/** \file
 * Virtual terminals: `/dev/tty1` to `/dev/tty6` (character device 4, 1 to
 * 6), and `/dev/console`, which is the first one.
 *
 * What programs write to a terminal goes through the terminal emulator
 * (`term.c`) to its screen, and key presses (`keyboard.c`) go through the
 * line discipline to its readers. Only the terminal in the foreground is
 * shown, on the framebuffer or in VGA text mode; the others keep their
 * screens in memory. `/dev/tty0` is the foreground terminal.
 *
 * `/dev/vcsN` and `/dev/vcsaN` (character device 7, minors N and 128 +
 * N; N = 0 is the foreground terminal) read the screen of `/dev/ttyN`, as
 * on Linux.
 *
 * Programs that draw on the framebuffer themselves (an X server) use the
 * Linux console's `ioctl()`s: `KDSETMODE` (with `KD_GRAPHICS`, the
 * terminal isn't drawn), `KDSKBMODE` (scancodes or key codes instead of
 * characters), and `VT_*` to switch terminals, or to be asked before a
 * switch (`VT_PROCESS`). Once the process that set these modes is gone,
 * they are undone; otherwise the screen would be left to no one.
 */

#include <core/system.h>
#include <core/module.h>
#include <core/string.h>
#include <console/earlycon.h>

#include <dev/dev.h>
#include <dev/vt.h>
#include <fs/posix.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/uaccess.h>

MALLOC_DEFINE(M_VT, "vt", "virtual terminal display");

/* the input ring's size: it holds a full line from the terminal
 * (TTY_BUF_SIZE), like the UART's */
#define VT_RING 4096

static struct vt vts[VT_COUNT];
static struct vt *fg;

static struct vt_display *display;

/* what the display shows, so that only what changed is drawn */
static struct vt_cell *shown;

/* kernel messages are written to the foreground terminal, unless it is
 * being written to already */
static int busy;

/* a switch to this terminal (its index) that waits for the foreground
 * terminal's VT_PROCESS process to allow it (VT_RELDISP); -1 if none */
static int pending = -1;

/* where VT_WAITACTIVE waits; woken up after each switch */
static struct waitq active_queue;

/* the 16 colours, as the Linux console has them (0xRRGGBB) */
static const uint32_t ansi_colors[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
};

/**
 * Returns one of the 256 colours of xterm's palette: the 16 colours, a 6 x
 * 6 x 6 cube, and 24 greys.
 *
 * @param color  the colour's number
 * @return the colour, as 0xRRGGBB
 */
uint32_t vt_palette(uint8_t color)
{
    if (color < 16)
        return ansi_colors[color];

    if (color >= 232) {
        uint32_t v = 8 + 10 * (color - 232);
        return v << 16 | v << 8 | v;
    }

    static const uint8_t levels[] = {0, 95, 135, 175, 215, 255};
    color -= 16;

    return levels[color / 36] << 16 | levels[color / 6 % 6] << 8 | levels[color % 6];
}

/** Returns the closest of the 16 colours to one of the 256. */
static uint8_t color16(uint8_t color)
{
    if (color < 16)
        return color;

    uint32_t rgb = vt_palette(color);
    int r = rgb >> 16, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    int best = 0, best_dist = 0x7fffffff;

    for (int i = 0; i < 16; ++i) {
        int dr = r - (int) (ansi_colors[i] >> 16);
        int dg = g - (int) ((ansi_colors[i] >> 8) & 0xff);
        int db = b - (int) (ansi_colors[i] & 0xff);
        int dist = dr * dr + dg * dg + db * db;

        if (dist < best_dist) {
            best = i;
            best_dist = dist;
        }
    }

    return best;
}

/**
 * Returns the VGA text mode attribute of a cell (its colours, in VGA's
 * order), for the VGA display and `/dev/vcsa`.
 *
 * @param cell  the cell
 * @return the attribute byte: the background in bits 4 to 6, the
 *         foreground in bits 0 to 3
 */
uint8_t vt_vga_attr(struct vt_cell cell)
{
    /* VGA's colours are in BGR order, ANSI's in RGB order */
    static const uint8_t vga[8] = {0, 4, 2, 6, 1, 5, 3, 7};
    uint8_t fg = color16(cell.fg), bg = color16(cell.bg);

    fg = (fg & 8) | vga[fg & 7];
    bg = vga[bg & 7];

    if ((cell.attr & VT_BOLD) && fg < 8)
        fg += 8;

    if (cell.attr & VT_REVERSE) {
        uint8_t t = fg & 7;
        fg = bg;
        bg = t;
    }

    if (cell.attr & VT_INVISIBLE)
        fg = bg;

    return bg << 4 | fg;
}

/** Draws the cells of the foreground terminal that changed, or all of
 * them with `all`, and moves the cursor. A terminal that isn't shown, or
 * is in graphics mode, isn't drawn. */
static void refresh(struct vt *vt, int all)
{
    if (vt != fg || !display || vt->kd_mode == KD_GRAPHICS)
        return;

    int cursor = vt->cursor_visible && !display->cursor;

    for (int row = 0; row < vt->rows; ++row) {
        for (int col = 0; col < vt->cols; ++col) {
            int i = row * vt->cols + col;
            struct vt_cell c = vt->screen[i];

            if (vt->reverse_screen)
                c.attr ^= VT_REVERSE;

            if (cursor && row == vt->row && col == vt->col)
                c.attr |= VT_CURSOR;

            if (all || memcmp(&c, &shown[i], sizeof(c))) {
                display->draw(row, col, c);
                shown[i] = c;
            }
        }
    }

    if (display->cursor)
        display->cursor(vt->row, vt->col, vt->cursor_visible);
}

/** Takes the terminal's output: it goes through the emulator to the
 * screen. */
static ssize_t vt_tty_output(struct tty *tty, size_t size, void *buf)
{
    vt_term_write(tty->p, buf, size);
    return size;
}

/** Takes the terminal's input, after line editing, for its readers. */
static ssize_t vt_tty_input(struct tty *tty, size_t size, void *buf)
{
    struct vt *vt = tty->p;
    return ringbuf_write(vt->in, size, buf);
}

/** Drops the input waiting to be read (`tcflush()`). */
static void vt_input_flush(struct tty *tty)
{
    struct vt *vt = tty->p;
    char c;

    while (ringbuf_read(vt->in, 1, &c))
        ;
}

/** Gives input to a terminal: key presses, or replies to requests. */
static void vt_input_to(struct vt *vt, const char *buf, size_t size)
{
    tty_master_write(vt->tty, size, (void *) buf);
    waitq_wakeup(&vt->read_queue);
}

/** Gives the replies to requests that programs wrote (DSR, DA) to the
 * terminal, as input. */
static void vt_reply(struct vt *vt)
{
    char buf[sizeof(vt->reply)];
    size_t len = vt->reply_len;

    if (!len)
        return;

    memcpy(buf, vt->reply, len);
    vt->reply_len = 0;

    vt_input_to(vt, buf, len);
}

/** Sets a terminal up the first time it is used: its screen, its input
 * ring and its terminal. Returns 0 or a negative errno. */
static int vt_setup(struct vt *vt)
{
    if (vt->tty)
        return 0;

    if (!display)
        return -ENXIO;

    int err;

    if ((err = vt_term_init(vt, display->rows, display->cols)))
        return err;

    vt->in = ringbuf_new(VT_RING);

    if (!vt->in)
        return -ENOMEM;

    if ((err = tty_new(NULL, 0, vt_tty_output, vt_tty_input, vt, &vt->tty)))
        return err;

    vt->tty->input_flush = vt_input_flush;
    vt->tty->ws.ws_row = vt->rows;
    vt->tty->ws.ws_col = vt->cols;

    return 0;
}

/**
 * Returns the terminal in the foreground.
 *
 * @return the terminal, or NULL before the terminals are set up
 */
struct vt *vt_foreground(void)
{
    return fg;
}

/** Returns whether a process is still running. */
static int proc_alive(pid_t pid)
{
    struct proc *proc = proc_pid_find(pid);
    return proc && proc->running;
}

/** Undoes the modes that a process which is gone had set on a terminal
 * (graphics mode, raw keyboard, `VT_PROCESS`). */
static void vt_check(struct vt *vt)
{
    if (vt->kd_pid && !proc_alive(vt->kd_pid)) {
        vt->kd_pid  = 0;
        vt->kb_mode = K_XLATE;
        vt_keyboard_reset();

        if (vt->kd_mode != KD_TEXT) {
            vt->kd_mode = KD_TEXT;
            refresh(vt, 1);
        }
    }

    if (vt->mode_pid && !proc_alive(vt->mode_pid)) {
        vt->mode_pid = 0;
        vt->mode = (struct vt_mode) {.mode = VT_AUTO};
    }
}

/** Sends a signal to the process that put a terminal in `VT_PROCESS`
 * mode. */
static void vt_signal(struct vt *vt, int signal)
{
    struct proc *proc = proc_pid_find(vt->mode_pid);

    if (proc && signal > 0 && signal < NSIG)
        signal_proc_send(proc, signal);
}

/** Switches to terminal `id` (an index) at once: it is set up if
 * needed, drawn, and its `VT_PROCESS` process is told. */
static void vt_switch_now(int id)
{
    struct vt *vt = &vts[id];

    pending = -1;

    if (vt_setup(vt))
        return;

    vt_check(vt);

    /* the keyboard's mode may be different there */
    if (fg && fg->kb_mode != vt->kb_mode)
        vt_keyboard_reset();

    fg = vt;

    if (vt->kd_mode == KD_TEXT)
        refresh(vt, 1);
    else if (display->cursor)
        display->cursor(0, 0, 0);

    if (vt->mode.mode == VT_PROCESS)
        vt_signal(vt, vt->mode.acqsig);

    waitq_wakeup(&active_queue);
}

/**
 * Switches to another terminal, for Alt+Fn and `VT_ACTIVATE`. If the
 * foreground terminal's process asked to decide (`VT_PROCESS`), it is
 * asked instead, and the switch waits for its answer (`VT_RELDISP`).
 *
 * @param id  the terminal's index: 0 for `/dev/tty1`
 */
void vt_switch(int id)
{
    if (id < 0 || id >= VT_COUNT || !fg || &vts[id] == fg)
        return;

    vt_check(fg);

    /* the process decides, with VT_RELDISP */
    if (fg->mode.mode == VT_PROCESS) {
        pending = id;
        vt_signal(fg, fg->mode.relsig);
        return;
    }

    vt_switch_now(id);
}

/**
 * Undoes, at a key press, the modes that a process which is gone had set
 * on the foreground terminal: otherwise the keyboard could be left to no
 * one.
 */
void vt_keypress(void)
{
    if (fg)
        vt_check(fg);
}

/**
 * Gives what keys sent to the foreground terminal, as input.
 *
 * @param buf   what the keys sent
 * @param size  its size, in bytes
 */
void vt_input(const char *buf, size_t size)
{
    if (!fg)
        return;

    busy++;
    vt_input_to(fg, buf, size);
    vt_reply(fg);
    refresh(fg, 0);
    busy--;
}

/** Writes kernel messages to the foreground terminal's screen. */
static void vt_kmsg(const char *s, size_t len)
{
    struct vt *vt = fg;

    if (!vt || busy)
        return;

    busy++;

    /* a message isn't part of what programs write: keep the state of the
     * sequence they may be in the middle of, and give it back after */
    int state = vt->state, utf8_left = vt->utf8_left;
    vt->state = 0;
    vt->utf8_left = 0;

    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '\n')
            vt_term_write(vt, "\r\n", 2);
        else
            vt_term_write(vt, &s[i], 1);
    }

    vt->state = state;
    vt->utf8_left = utf8_left;

    refresh(vt, 0);
    busy--;
}

/** Returns the terminal of a device number: `/dev/console` is the first
 * one, `/dev/tty0` the one in the foreground. */
static struct vt *vt_get(struct devid *dd)
{
    if (dd->major == 5)
        return &vts[0];

    if (dd->minor == 0)
        return fg;

    if (dd->minor < 1 || dd->minor > VT_COUNT)
        return NULL;

    return &vts[dd->minor - 1];
}

/** Returns the terminal of an open file. */
static struct vt *vt_file(struct file *file)
{
    return vt_get(&VNODE_DEV(file->vnode));
}

/** Reads a terminal's input. A process of a background group gets
 * `SIGTTIN` instead. */
static ssize_t vt_read(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct vt *vt = vt_get(dd);

    if (!vt || !vt->tty)
        return -EIO;

    int err = tty_background(vt->tty, SIGTTIN);
    if (err) return err;

    return ringbuf_read_iobuf(vt->in, io->resid, io);
}

/** Writes to a terminal's screen. With `TOSTOP`, a process of a
 * background group gets `SIGTTOU` instead. */
static ssize_t vt_write(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct vt *vt = vt_get(dd);

    if (!vt || !vt->tty)
        return -EIO;

    if (vt->tty->tios.c_lflag & TOSTOP) {
        int err = tty_background(vt->tty, SIGTTOU);

        if (err)
            return err;
    }

    busy++;
    ssize_t ret = tty_write_iobuf(vt->tty, io, tty_slave_write);
    vt_reply(vt);
    refresh(vt, 0);
    busy--;

    return ret;
}

/** Returns whether a terminal is in use, so that `VT_OPENQRY` doesn't
 * give it. */
static int vt_in_use(struct vt *vt)
{
    return vt->tty && (vt->opens || vt->tty->session);
}

/** Returns the size of the user memory that a `KD*` or `VT_*` request
 * reads or writes, or 0 if its argument is a value. */
static size_t vt_ioctl_argsize(int request)
{
    switch (request) {
        case KDGKBTYPE:
            return sizeof(char);
        case KDGETMODE:
        case KDGKBMODE:
        case VT_OPENQRY:
            return sizeof(int);
        case VT_GETMODE:
        case VT_SETMODE:
            return sizeof(struct vt_mode);
        case VT_GETSTATE:
            return sizeof(struct vt_stat);
        case KDGKBENT:
            return sizeof(struct kbentry);
    }

    return 0;
}

/** Handles the Linux console's `KD*` and `VT_*` requests. The argument is
 * a value, or a pointer to the kernel's copy of the user's memory
 * (`vt_ioctl()` copies it in and out). */
static int vt_kd_ioctl(struct vt *vt, int request, void *argp)
{
    int arg = (int) (uintptr_t) argp;

    switch (request) {
        case KDGKBTYPE:
            *(char *) argp = KB_101;
            return 0;

        case KDGETMODE:
            *(int *) argp = vt->kd_mode;
            return 0;

        case KDSETMODE:
            if (arg != KD_TEXT && arg != KD_GRAPHICS)
                return -EINVAL;

            if (arg == vt->kd_mode)
                return 0;

            vt->kd_mode = arg;

            if (arg == KD_GRAPHICS) {
                vt->kd_pid = curproc->pid;

                if (vt == fg && display->cursor)
                    display->cursor(0, 0, 0);
            } else {
                refresh(vt, 1);
            }

            return 0;

        case KDGKBMODE:
            *(int *) argp = vt->kb_mode;
            return 0;

        case KDGKBENT: {
            struct kbentry *kbe = argp;
            kbe->kb_value = vt_keymap(kbe->kb_table, kbe->kb_index);
            return 0;
        }

        /* there are no lights or speaker, and no terminal is ever freed */
        case KDSETLED:
        case KDMKTONE:
        case VT_DISALLOCATE:
            return 0;

        case KDSKBMODE:
            if (arg < K_RAW || arg > K_OFF)
                return -EINVAL;

            if (vt->kb_mode != arg && vt == fg)
                vt_keyboard_reset();

            vt->kb_mode = arg;

            if (arg != K_XLATE && arg != K_UNICODE)
                vt->kd_pid = curproc->pid;

            return 0;

        case VT_OPENQRY: {
            int id = -1;

            /* not the first: it is the console (kernel messages) */
            for (int i = 1; i < VT_COUNT && id < 0; ++i)
                if (!vt_in_use(&vts[i]))
                    id = i + 1;

            *(int *) argp = id;
            return 0;
        }

        case VT_GETMODE:
            vt_check(vt);
            *(struct vt_mode *) argp = vt->mode;
            return 0;

        case VT_SETMODE: {
            struct vt_mode mode = *(struct vt_mode *) argp;

            if (mode.mode != VT_AUTO && mode.mode != VT_PROCESS)
                return -EINVAL;

            vt->mode = mode;
            vt->mode_pid = mode.mode == VT_PROCESS ? curproc->pid : 0;

            /* nothing waits for the process anymore: do the pending
             * switch */
            if (mode.mode == VT_AUTO && vt == fg && pending >= 0)
                vt_switch_now(pending);

            return 0;
        }

        case VT_GETSTATE: {
            struct vt_stat *stat = argp;

            stat->v_active = fg ? fg->id : 0;
            stat->v_signal = 0;
            stat->v_state  = 1;     /* bit 0, as on Linux */

            for (int i = 0; i < VT_COUNT; ++i)
                if (vt_in_use(&vts[i]))
                    stat->v_state |= 1 << (i + 1);

            return 0;
        }

        case VT_RELDISP:
            if (vt->mode.mode != VT_PROCESS || vt != fg)
                return -EINVAL;

            if (arg == VT_ACKACQ)
                return 0;

            if (pending < 0)
                return -EINVAL;

            if (arg)
                vt_switch_now(pending);
            else
                pending = -1;

            return 0;

        case VT_ACTIVATE:
            if (arg < 1 || arg > VT_COUNT)
                return -ENXIO;

            vt_switch(arg - 1);
            return 0;

        case VT_WAITACTIVE:
            if (arg < 0 || arg > VT_COUNT)
                return -ENXIO;

            while (arg && fg != &vts[arg - 1]) {
                int err = waitq_sleep(&active_queue);

                if (err)
                    return err;
            }

            return 0;
    }

    return -ENOTTY;
}

/** Handles a terminal's `ioctl()`s: the console's own (`KD*`, `VT_*`),
 * through a kernel copy of their argument, and the terminal's. */
static int vt_ioctl(struct devid *dd, int request, void *argp)
{
    struct vt *vt = vt_get(dd);

    if (!vt || !vt->tty)
        return -EIO;

    /* KD* requests are 0x4Bxx, VT_* ones 0x56xx */
    if ((request >> 8) == 0x4B || (request >> 8) == 0x56) {
        size_t size = vt_ioctl_argsize(request);

        if (!size)
            return vt_kd_ioctl(vt, request, argp);

        /* go through the kernel's copy: copied in, then back out */
        union {
            char c;
            int i;
            struct vt_mode mode;
            struct vt_stat stat;
            struct kbentry kbe;
        } arg;

        if (copyin(&arg, argp, size))
            return -EFAULT;

        int err = vt_kd_ioctl(vt, request, &arg);
        return err ? err : copyout(argp, &arg, size);
    }

    return tty_ioctl(vt->tty, request, argp);
}

/** Opens a terminal, setting it up the first time. It may become the
 * process's controlling terminal (`tty_open()`). */
static int vt_file_open(struct file *file)
{
    struct vt *vt = vt_file(file);

    if (!vt)
        return -ENXIO;

    int err = vt_setup(vt);
    if (err) return err;

    /* /dev/tty0 is whichever terminal is in the foreground: it uses that
     * one's queues, and isn't a terminal of its own */
    if (DEV_MAJOR(file->vnode->rdev) == 4 && DEV_MINOR(file->vnode->rdev) == 0) {
        file->vnode->read_queue  = &vt->read_queue;
        file->vnode->write_queue = &vt->write_queue;
        return 0;
    }

    vt->opens++;

    /* every node of the terminal shares the same queues */
    if (!vt->vnode) {
        vt->vnode = file->vnode;
        vt->tty->vnode = file->vnode;
    }

    file->vnode->read_queue  = &vt->read_queue;
    file->vnode->write_queue = &vt->write_queue;

    /* /dev/console never becomes a controlling terminal, as on Linux:
     * init writes to it, and the logins on the terminal get it */
    if (DEV_MAJOR(file->vnode->rdev) != 5)
        tty_open(vt->tty, file);

    return 0;
}

/** Closes a terminal. */
static int vt_file_close(struct file *file)
{
    struct vt *vt = vt_file(file);
    dev_t rdev = file->vnode->rdev;

    /* there is one close for each open (whatever descriptors shared the
     * file); the opens of /dev/tty0 weren't counted */
    if (vt && vt->opens > 0 && !(DEV_MAJOR(rdev) == 4 && DEV_MINOR(rdev) == 0))
        vt->opens--;

    return posix_file_close(file);
}

/** Returns whether a read that found no data is at the end of the file. */
static int vt_file_eof(struct file *file)
{
    struct vt *vt = vt_file(file);
    return !vt || !vt->tty || tty_eof(vt->tty);
}

/** Returns how long a read may wait, from the terminal's mode (`VTIME`),
 * or 0 for no limit. */
static uint64_t vt_read_timeout(struct file *file)
{
    struct vt *vt = vt_file(file);
    return vt && vt->tty ? tty_read_timeout(vt->tty) : 0;
}

/** Returns whether a read wouldn't have to wait. */
static int vt_can_read(struct file *file, size_t size __unused)
{
    struct vt *vt = vt_file(file);
    return !vt || !vt->tty || tty_can_read(vt->tty, ringbuf_available(vt->in));
}

struct dev vtdev = {
    .name = "vt",

    .read  = vt_read,
    .write = vt_write,
    .ioctl = vt_ioctl,

    .fops = {
        .open  = vt_file_open,
        .close = vt_file_close,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = vt_can_read,
        .can_write = __vfs_can_always,
        .eof       = vt_file_eof,
        .read_timeout = vt_read_timeout,
    },
};

/**
 * Reads a terminal's screen. `/dev/vcsN` gives the characters of the
 * screen, row after row. `/dev/vcsaN` gives the rows, the columns, and
 * the cursor's column and row (a byte each), then each character with its
 * VGA attribute.
 */
static ssize_t vcs_read(struct devid *dd, off_t offset, struct iobuf *io)
{
    int attrs = dd->minor & 128, id = dd->minor & 127;
    struct vt *vt = id ? (id <= VT_COUNT ? &vts[id - 1] : NULL) : fg;
    uint8_t buf[256];

    if (!vt)
        return -ENXIO;

    if (!vt->tty)
        return 0;

    size_t cells = vt->rows * vt->cols;
    size_t len = attrs ? 4 + 2 * cells : cells;
    size_t size = io->resid, done = 0;

    if (offset < 0)
        return -EINVAL;

    /* a piece at a time, out through the iobuf */
    for (size_t i = offset; i < len && done < size; ) {
        size_t n = 0;

        for (; i < len && done + n < size && n < sizeof(buf); ++i, ++n) {
            if (attrs && i < 4) {
                uint8_t header[] = {vt->rows, vt->cols, vt->col, vt->row};
                buf[n] = header[i];
            } else if (attrs) {
                struct vt_cell c = vt->screen[(i - 4) / 2];
                buf[n] = (i - 4) % 2 ? vt_vga_attr(c) : c.ch;
            } else {
                buf[n] = vt->screen[i].ch;
            }
        }

        ssize_t r = iobuf_out(io, buf, n);

        if (r < 0)
            return done ? (ssize_t) done : r;

        done += r;

        if ((size_t) r < n)
            break;
    }

    return done;
}

static struct dev vcsdev = {
    .name = "vcs",
    .read = vcs_read,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .lseek = posix_file_lseek,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_never,
        .eof       = __vfs_eof_always,
    },
};

/** Shows the kernel messages printed so far, before the terminals were
 * set up. */
static void vt_kmsg_replay(void)
{
    extern struct ringbuf *kmsg;
    char buf[128];
    size_t len = ringbuf_available(kmsg);

    for (size_t off = 0; off < len; ) {
        size_t n = ringbuf_read_noconsume(kmsg, off, MIN(sizeof(buf), len - off), buf);

        if (!n)
            break;

        vt_kmsg(buf, n);
        off += n;
    }
}

/** Sets the virtual terminals up at boot, on the framebuffer or in VGA
 * text mode, and shows the first one, with the kernel messages. */
static int vt_probe(void)
{
    if (vt_fb_probe() == 0)
        display = &vt_fb_display;
    else if (vt_vga_probe() == 0)
        display = &vt_vga_display;
    else
        return -ENODEV;

    shown = kmalloc(display->rows * display->cols * sizeof(struct vt_cell), &M_VT, 0);

    if (!shown) {
        display = NULL;
        return -ENOMEM;
    }

    for (int i = 0; i < VT_COUNT; ++i) {
        vts[i].id = i + 1;
        vts[i].kb_mode = K_XLATE;
    }

    vt_switch_now(0);

    if (!fg) {
        display = NULL;
        return -ENOMEM;
    }

    /* the terminal shows the kernel messages from now on, instead of the
     * early console if that was on the screen */
    if (earlycon_on_screen())
        earlycon_disable();

    vt_kmsg_replay();
    printk_console(vt_kmsg);

    kdev_chrdev_register(7, &vcsdev);

    printk("vt: %d terminals of %dx%d on %s\n", VT_COUNT, display->cols, display->rows, display->name);
    return 0;
}

MODULE_INIT(vt, vt_probe, NULL)
