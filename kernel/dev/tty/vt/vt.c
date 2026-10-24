/** \file
 * Virtual terminals: /dev/tty1 to /dev/tty6 (major 4), /dev/console is
 * the first one. What programs write goes through the terminal emulator
 * (term.c) to the screen, key presses (keyboard.c) through the line
 * discipline to readers. Only the terminal in the foreground is shown,
 * the others keep their screens.
 *
 * /dev/vcsN and /dev/vcsaN (major 7, minors N and 128 + N, 0 is the
 * foreground terminal) read the screen of tty N, as on Linux.
 *
 * For programs that draw on the framebuffer themselves (an X server), the
 * ioctls of the Linux console: KDSETMODE (KD_GRAPHICS, the terminal isn't
 * drawn), KDSKBMODE (scancodes or key codes instead of characters), and
 * VT_* to switch terminals, or to be asked first (VT_PROCESS). /dev/tty0
 * is the foreground terminal. Once the process that set these modes is
 * gone, they are undone (the screen would be left to no one).
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

/* holds a full line from the tty (TTY_BUF_SIZE), like the uart */
#define VT_RING 4096

static struct vt vts[VT_COUNT];
static struct vt *fg;

static struct vt_display *display;

/* what the display shows, to only draw what changed */
static struct vt_cell *shown;

/* kernel messages are written to the foreground terminal, unless it is
 * already being written to */
static int busy;

/* a switch to this terminal (an index), waiting for the foreground's
 * VT_PROCESS process to release it (VT_RELDISP), -1 if none */
static int pending = -1;

/* VT_WAITACTIVE, woken up after each switch */
static struct queue active_queue;

static const uint32_t ansi_colors[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
};

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

/** the closest of the 16 colors */
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

/** the VGA text mode attribute of a cell, for the VGA display and vcsa */
uint8_t vt_vga_attr(struct vt_cell cell)
{
    /* VGA colors are BGR, ANSI colors RGB */
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

/** draws the cells of the foreground terminal that changed */
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

/** tty interface: output to the screen */
static ssize_t vt_tty_output(struct tty *tty, size_t size, void *buf)
{
    vt_term_write(tty->p, buf, size);
    return size;
}

/** tty interface: input for readers */
static ssize_t vt_tty_input(struct tty *tty, size_t size, void *buf)
{
    struct vt *vt = tty->p;
    return ringbuf_write(vt->in, size, buf);
}

static void vt_input_flush(struct tty *tty)
{
    struct vt *vt = tty->p;
    char c;

    while (ringbuf_read(vt->in, 1, &c))
        ;
}

/** input to a terminal, from the keyboard or replies to requests */
static void vt_input_to(struct vt *vt, const char *buf, size_t size)
{
    tty_master_write(vt->tty, size, (void *) buf);
    thread_queue_wakeup(&vt->read_queue);
}

/** replies to requests in what was written (DSR, DA) are input */
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

/** a terminal is used for the first time */
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

struct vt *vt_foreground(void)
{
    return fg;
}

static int proc_alive(pid_t pid)
{
    struct proc *proc = proc_pid_find(pid);
    return proc && proc->running;
}

/** the modes of a process that is gone are undone */
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

/** the process of a terminal in VT_PROCESS mode gets a signal */
static void vt_signal(struct vt *vt, int signal)
{
    struct proc *proc = proc_pid_find(vt->mode_pid);

    if (proc && signal > 0 && signal < NSIG)
        signal_proc_send(proc, signal);
}

static void vt_switch_now(int id)
{
    struct vt *vt = &vts[id];

    pending = -1;

    if (vt_setup(vt))
        return;

    vt_check(vt);

    /* what the keyboard sends may be different there */
    if (fg && fg->kb_mode != vt->kb_mode)
        vt_keyboard_reset();

    fg = vt;

    if (vt->kd_mode == KD_TEXT)
        refresh(vt, 1);
    else if (display->cursor)
        display->cursor(0, 0, 0);

    if (vt->mode.mode == VT_PROCESS)
        vt_signal(vt, vt->mode.acqsig);

    thread_queue_wakeup(&active_queue);
}

void vt_switch(int id)
{
    if (id < 0 || id >= VT_COUNT || !fg || &vts[id] == fg)
        return;

    vt_check(fg);

    /* the process decides, VT_RELDISP */
    if (fg->mode.mode == VT_PROCESS) {
        pending = id;
        vt_signal(fg, fg->mode.relsig);
        return;
    }

    vt_switch_now(id);
}

void vt_keypress(void)
{
    if (fg)
        vt_check(fg);
}

/** key presses, to the foreground terminal */
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

/** kernel messages */
static void vt_kmsg(const char *s, size_t len)
{
    struct vt *vt = fg;

    if (!vt || busy)
        return;

    busy++;

    /* not part of what programs write, whatever sequence they are in */
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

/** /dev/console is the first terminal, /dev/tty0 the foreground one */
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

static struct vt *vt_file(struct file *file)
{
    return vt_get(&VNODE_DEV(file->vnode));
}

static ssize_t vt_read(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct vt *vt = vt_get(dd);

    if (!vt || !vt->tty)
        return -EIO;

    int err = tty_background(vt->tty, SIGTTIN);
    if (err) return err;

    return ringbuf_read_iobuf(vt->in, io->resid, io);
}

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

/** a terminal is in use, VT_OPENQRY doesn't give it */
static int vt_in_use(struct vt *vt)
{
    return vt->tty && (vt->opens || vt->tty->session);
}

/** the size of the user buffer an ioctl request reads or writes (0: the
 * argument is a value) */
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

/** KD* and VT_*, as on Linux: the argument is a value, or a pointer to the
 * kernel's copy of it (vt_ioctl) */
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

        /* no lights or speaker, no terminal is ever freed */
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

            /* not the first, the console (kernel messages) */
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

            /* nothing waits for the process anymore */
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
                int err = thread_queue_sleep(&active_queue);

                if (err)
                    return err;
            }

            return 0;
    }

    return -ENOTTY;
}

static int vt_ioctl(struct devid *dd, int request, void *argp)
{
    struct vt *vt = vt_get(dd);

    if (!vt || !vt->tty)
        return -EIO;

    /* KD* are 0x4Bxx, VT_* 0x56xx */
    if ((request >> 8) == 0x4B || (request >> 8) == 0x56) {
        size_t size = vt_ioctl_argsize(request);

        if (!size)
            return vt_kd_ioctl(vt, request, argp);

        /* through the kernel's copy: in, then back out */
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

static int vt_file_open(struct file *file)
{
    struct vt *vt = vt_file(file);

    if (!vt)
        return -ENXIO;

    int err = vt_setup(vt);
    if (err) return err;

    /* /dev/tty0, whichever terminal is in the foreground: the queues of
     * this one, and it isn't a terminal of its own */
    if (DEV_MAJOR(file->vnode->rdev) == 4 && DEV_MINOR(file->vnode->rdev) == 0) {
        file->vnode->read_queue  = &vt->read_queue;
        file->vnode->write_queue = &vt->write_queue;
        return 0;
    }

    vt->opens++;

    /* the same queues for every node of the terminal */
    if (!vt->vnode) {
        vt->vnode = file->vnode;
        vt->tty->vnode = file->vnode;
    }

    file->vnode->read_queue  = &vt->read_queue;
    file->vnode->write_queue = &vt->write_queue;

    /* /dev/console never becomes a controlling terminal (as on Linux):
     * init writes to it, logins on the terminal get it */
    if (DEV_MAJOR(file->vnode->rdev) != 5)
        tty_open(vt->tty, file);

    return 0;
}

static int vt_file_close(struct file *file)
{
    struct vt *vt = vt_file(file);
    dev_t rdev = file->vnode->rdev;

    /* one close for each open (whatever descriptors shared the file);
     * /dev/tty0's weren't counted */
    if (vt && vt->opens > 0 && !(DEV_MAJOR(rdev) == 4 && DEV_MINOR(rdev) == 0))
        vt->opens--;

    return posix_file_close(file);
}

static int vt_file_eof(struct file *file)
{
    struct vt *vt = vt_file(file);
    return !vt || !vt->tty || tty_eof(vt->tty);
}

static uint64_t vt_read_timeout(struct file *file)
{
    struct vt *vt = vt_file(file);
    return vt && vt->tty ? tty_read_timeout(vt->tty) : 0;
}

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
 * /dev/vcsN: the characters of the screen, row after row. /dev/vcsaN:
 * rows, columns, and the cursor's column and row (a byte each), then the
 * characters with their VGA attributes.
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

/** the kernel messages so far, the screen showed them already */
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

    /* the terminal shows kernel messages from now on, instead of the early
     * console if it was on the screen */
    if (earlycon_on_screen())
        earlycon_disable();

    vt_kmsg_replay();
    printk_console(vt_kmsg);

    kdev_chrdev_register(7, &vcsdev);

    printk("vt: %d terminals of %dx%d on %s\n", VT_COUNT, display->cols, display->rows, display->name);
    return 0;
}

MODULE_INIT(vt, vt_probe, NULL)
