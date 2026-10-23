#include <core/system.h>
#include <core/arch.h>
#include <sys/sched.h>
#include <dev/tty.h>
#include <sys/uaccess.h>
#include <sys/signal.h>
#include <aquila/fcntl.h>

MALLOC_DEFINE(M_TTY, "tty", "tty structure");
MALLOC_DEFINE(M_TTY_COOK, "tty-cook", "tty cooking buffer");

/* a special character, not disabled (_POSIX_VDISABLE is 0) */
#define IS_CC(tty, c, cc) ((tty)->tios.c_cc[cc] && (c) == (tty)->tios.c_cc[cc])

/** echo a character, control characters as ^X */
static void tty_echo(struct tty *tty, char c)
{
    if ((unsigned char) c < ' ' && c != '\n' && c != '\t') {
        char cc[] = {'^', c + '@'};
        tty_slave_write(tty, 2, cc);
    } else if (c == 0x7f) {
        tty_slave_write(tty, 2, "^?");
    } else {
        tty_slave_write(tty, 1, &c);
    }
}

/** erase the last character of the line being edited */
static void tty_erase(struct tty *tty)
{
    if (!tty->pos)
        return;

    char c = tty->cook[--tty->pos];

    if ((tty->tios.c_lflag & ECHO) && (tty->tios.c_lflag & ECHOE)) {
        /* control characters were echoed as two */
        int width = ((unsigned char) c < ' ' && c != '\t') || c == 0x7f ? 2 : 1;

        while (width--)
            tty_slave_write(tty, 3, "\b \b");
    }
}

/** the line is ready to be read */
static void tty_deliver(struct tty *tty)
{
    if (tty->pos)
        tty->slave_write(tty, tty->pos, tty->cook);

    tty->pos = 0;
}

/** signal characters (ISIG): the foreground group gets the signal */
static int tty_signal(struct tty *tty, char c)
{
    int sig = IS_CC(tty, c, VINTR) ? SIGINT :
              IS_CC(tty, c, VQUIT) ? SIGQUIT :
              IS_CC(tty, c, VSUSP) ? SIGTSTP : 0;

    if (!sig)
        return 0;

    if (!(tty->tios.c_lflag & NOFLSH))
        tty->pos = 0;

    if (tty->tios.c_lflag & ECHO) {
        tty_echo(tty, c);
        tty_slave_write(tty, 1, "\n");
    }

    /* only queued: this runs from the driver, maybe in an interrupt */
    if (tty->fg)
        signal_pgrp_send(tty->fg, sig);

    return 1;
}

/**
 * Input from the terminal (the keyboard, or the master side of a pty),
 * through the line discipline to what programs read. Canonical mode
 * collects lines with erase (VERASE), kill (VKILL) and end-of-file
 * (VEOF), others get the characters as they come.
 */
ssize_t tty_master_write(struct tty *tty, size_t size, void *buf)
{
    struct termios *tios = &tty->tios;
    char *in = buf;

    for (size_t i = 0; i < size; ++i) {
        char c = in[i];

        /* input translation */
        if (c == '\r') {
            if (tios->c_iflag & IGNCR)
                continue;
            if (tios->c_iflag & ICRNL)
                c = '\n';
        } else if (c == '\n' && (tios->c_iflag & INLCR)) {
            c = '\r';
        }

        /* flow control: VSTOP stops the output, VSTART (with IXANY, any
         * character) starts it again; neither is input */
        if (tios->c_iflag & IXON) {
            if (tty->stopped && (IS_CC(tty, c, VSTART) || (tios->c_iflag & IXANY))) {
                tty->stopped = 0;
                thread_queue_wakeup(&tty->start_wait);

                if (IS_CC(tty, c, VSTART))
                    continue;
            } else if (IS_CC(tty, c, VSTART)) {
                continue;
            } else if (IS_CC(tty, c, VSTOP)) {
                tty->stopped = 1;
                continue;
            }
        }

        if ((tios->c_lflag & ISIG) && tty_signal(tty, c))
            continue;

        if (!(tios->c_lflag & ICANON)) {
            tty->slave_write(tty, 1, &c);

            if (tios->c_lflag & ECHO)
                tty_echo(tty, c);

            continue;
        }

        if (IS_CC(tty, c, VERASE)) {
            tty_erase(tty);
        } else if (IS_CC(tty, c, VKILL)) {
            while (tty->pos)
                tty_erase(tty);
        } else if (IS_CC(tty, c, VEOF)) {
            /* the line so far without a newline, at the start of a line
             * the next read returns 0 */
            if (!tty->pos)
                tty->eof++;

            tty_deliver(tty);
        } else if (c == '\n' || IS_CC(tty, c, VEOL)) {
            tty->cook[tty->pos++] = c;

            if ((tios->c_lflag & ECHO) || (c == '\n' && (tios->c_lflag & ECHONL)))
                tty_echo(tty, c);

            tty_deliver(tty);
        } else {
            /* keep room for the terminating newline, drop what doesn't fit */
            if (tty->pos + 1 >= tty->size)
                continue;

            tty->cook[tty->pos++] = c;

            if (tios->c_lflag & ECHO)
                tty_echo(tty, c);
        }
    }

    return size;
}

/**
 * A read that found nothing to return: 1 if it returns 0, at an
 * end-of-file (VEOF at the start of a line) or with VMIN 0 in
 * non-canonical mode.
 */
int tty_eof(struct tty *tty)
{
    /* VMIN 0 with VTIME: the read waits (tty_read_timeout()) */
    if (!(tty->tios.c_lflag & ICANON))
        return !tty->tios.c_cc[VMIN] && !tty->tios.c_cc[VTIME];

    if (tty->eof) {
        tty->eof--;
        return 1;
    }

    return 0;
}

/** how long a read waits for input: VTIME tenths of a second with VMIN 0
 * in non-canonical mode, otherwise until there is some */
uint64_t tty_read_timeout(struct tty *tty)
{
    if ((tty->tios.c_lflag & ICANON) || tty->tios.c_cc[VMIN] || !tty->tios.c_cc[VTIME])
        return 0;

    return tty->tios.c_cc[VTIME] * 100000000ULL;
}

/** whether a read wouldn't wait, given what is buffered */
int tty_can_read(struct tty *tty, size_t buffered)
{
    return buffered || tty->eof ||
           (!(tty->tios.c_lflag & ICANON) && !tty->tios.c_cc[VMIN] && !tty->tios.c_cc[VTIME]);
}

/**
 * A process of the terminal's session that is not in its foreground
 * group reads it (SIGTTIN) or writes or changes it (SIGTTOU, writes only
 * with TOSTOP): its group is stopped until it is in the foreground.
 * Returns -EIO for a read with SIGTTIN ignored or blocked (a write goes
 * on then); -ERESTARTSYS once it's sent: the call returns, the group stops
 * on the way out, and the call is made again once continued.
 */
int tty_background(struct tty *tty, int sig)
{
    struct proc *proc = curproc;

    if (tty->session && proc->pgrp->session == tty->session &&
        tty->fg && proc->pgrp != tty->fg) {
        if ((proc->sigmask & (1UL << sig)) || proc->sigaction[sig].sa_handler == SIG_IGN)
            return sig == SIGTTIN ? -EIO : 0;

        /* an orphaned group isn't stopped, no one would continue it */
        if (pgrp_orphaned(proc->pgrp, NULL))
            return -EIO;

        signal_pgrp_send(proc->pgrp, sig);
        return -ERESTARTSYS;
    }

    return 0;
}

/** `n' bytes to the device, all of them */
static int tty_out(struct tty *tty, const char *s, size_t n)
{
    return tty->master_write(tty, n, (void *) s) == (ssize_t) n;
}

/**
 * What the terminal writes out (OPOST, POSIX's output modes): a NL as CR
 * NL (ONLCR), a CR as NL (OCRNL), no CR in the first column (ONOCR), a NL
 * that returns the carriage too (ONLRET); the column is followed for them.
 * How many of `buf' went, the device may take fewer.
 */
ssize_t tty_slave_write(struct tty *tty, size_t size, void *_buf)
{
    const char *buf = (const char *) _buf;
    tcflag_t oflag = tty->tios.c_oflag;

    if (!(oflag & OPOST))
        return tty->master_write(tty, size, _buf);

    size_t written;

    for (written = 0; written < size; ++written) {
        char c = buf[written];

        if (c == '\n') {
            if (oflag & ONLCR) {
                /* CR NL; the CR alone went last time: the NL now */
                if (tty->nl_owed) {
                    if (!tty_out(tty, "\n", 1))
                        break;
                } else {
                    ssize_t r = tty->master_write(tty, 2, "\r\n");

                    if (r != 2) {
                        tty->nl_owed = r == 1;
                        if (r == 1)
                            tty->col = 0;
                        break;
                    }
                }

                tty->nl_owed = 0;
                tty->col = 0;
            } else {
                if (!tty_out(tty, "\n", 1))
                    break;

                if (oflag & ONLRET)
                    tty->col = 0;
            }
        } else if (c == '\r') {
            /* nothing to return from in the first column */
            if ((oflag & ONOCR) && !tty->col)
                continue;

            if (oflag & OCRNL) {
                if (!tty_out(tty, "\n", 1))
                    break;

                if (oflag & ONLRET)
                    tty->col = 0;
            } else {
                if (!tty_out(tty, "\r", 1))
                    break;

                tty->col = 0;
            }
        } else {
            if (!tty_out(tty, &c, 1))
                break;

            if (c == '\t')
                tty->col = (tty->col | 7) + 1;
            else if (c == '\b')
                tty->col -= !!tty->col;
            else if ((unsigned char) c >= ' ' && c != 0x7f)
                tty->col++;
        }
    }

    return written;
}

/**
 * A write to the terminal from an iobuf, to either side (`write':
 * tty_master_write or tty_slave_write): a piece at a time through the
 * kernel, the buffer passed over only as far as was taken
 */
static void tty_release(struct tty *tty);

/**
 * A program's output waits while the terminal is stopped (IXON): 0 once
 * it's started, -ERESTARTSYS for a signal, -EIO if the terminal went
 * meanwhile (its last waiter frees it)
 */
static int tty_wait_start(struct tty *tty)
{
    int err = 0;

    while (tty->stopped && !tty->gone && !err) {
        tty->start_waiters++;
        err = thread_queue_sleep(&tty->start_wait);
        tty->start_waiters--;
    }

    if (tty->gone) {
        if (!tty->start_waiters)
            tty_release(tty);

        return err ? err : -EIO;
    }

    return err;
}

ssize_t tty_write_iobuf(struct tty *tty, struct iobuf *io, ttyio write)
{
    char buf[256];
    size_t size = io->resid, done = 0;

    /* a program's output, to a stopped terminal: it waits */
    if (write == tty_slave_write && tty->stopped) {
        int err = tty_wait_start(tty);

        if (err)
            return err;
    }

    while (done < size) {
        ssize_t len = iobuf_peek(io, buf, MIN(size - done, sizeof(buf)));

        if (len <= 0)
            return done ? (ssize_t) done : len;

        ssize_t r = write(tty, len, buf);

        if (r < 0)
            return done ? (ssize_t) done : r;

        iobuf_skip(io, r);
        done += r;

        /* it took no more, or a fault stopped the copy */
        if (r < len || io->fault)
            break;
    }

    return done;
}

/** argp is user space's: what a request reads comes in with copyin(),
 * what it gives goes out with copyout() */
int tty_ioctl(struct tty *tty, int request, void *argp)
{
    int err;

    switch (request) {
        case TCGETS:
            return copyout(argp, &tty->tios, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:   /* output is never waiting to be sent */
        case TCSETSF: {
            struct termios tios;

            if (copyin(&tios, argp, sizeof(tios)))
                return -EFAULT;

            if ((err = tty_background(tty, SIGTTOU)))
                return err;

            if (request == TCSETSF)
                tty_flush(tty, TCIFLUSH);

            tty->tios = tios;

            /* no flow control anymore: what was stopped goes */
            if (!(tios.c_iflag & IXON) && tty->stopped) {
                tty->stopped = 0;
                thread_queue_wakeup(&tty->start_wait);
            }
            break;
        }
        case TCFLSH:
            /* the argument is the value: TCIFLUSH, TCOFLUSH, TCIOFLUSH */
            if ((uintptr_t) argp > TCIOFLUSH)
                return -EINVAL;

            tty_flush(tty, (uintptr_t) argp);
            break;
        case TCSBRK:
        case TCXONC:
            /* no output waits to be sent, there is no flow control */
            break;
        case TIOCGSID:
            if (!tty->session)
                return -ENOTTY;

            return copyout(argp, &tty->session->sid, sizeof(pid_t));
        case TIOCGPGRP: {
            /* without one, a value that is no process group (POSIX) */
            pid_t pgid = tty->fg ? tty->fg->pgid : 0x7fffffff;
            return copyout(argp, &pgid, sizeof(pgid));
        }
        case TIOCSPGRP: {
            /* the caller's controlling terminal, a group of its session */
            pid_t pgid;
            struct session *session = curproc->pgrp->session;
            struct pgroup *pgrp = NULL;

            if (copyin(&pgid, argp, sizeof(pgid)))
                return -EFAULT;

            if (session->ctty != tty)
                return -ENOTTY;

            if ((err = tty_background(tty, SIGTTOU)))
                return err;

            queue_for (node, session->pgps) {
                struct pgroup *pg = node->value;
                if (pg->pgid == pgid) {
                    pgrp = pg;
                    break;
                }
            }

            if (!pgrp)
                return -EPERM;

            tty->fg = pgrp;
            break;
        }
        case TIOCGWINSZ:
            return copyout(argp, &tty->ws, sizeof(struct winsize));
        case TIOCSWINSZ: {
            struct winsize ws;

            if (copyin(&ws, argp, sizeof(ws)))
                return -EFAULT;

            tty->ws = ws;
            break;
        }
        case TIOCSCTTY: {
            /* a session leader without one takes a terminal no other
             * session has */
            struct session *session = curproc->pgrp->session;

            if (session->leader != curproc || session->ctty || tty->session)
                return -EPERM;

            session->ctty = tty;
            tty->session  = session;
            tty->fg       = curproc->pgrp;
            break;
        }
        default:
            /* a request terminals don't have, as POSIX and Linux say */
            return -ENOTTY;
    }
    
    return 0;
}

/**
 * create a new generic tty interface
 */
int tty_new(struct proc *proc, size_t buf_size, ttyio master,
        ttyio slave, void *p, struct tty **ref)
{
    struct tty *tty = NULL;
    
    tty = kmalloc(sizeof(struct tty), &M_TTY, M_ZERO);
    if (!tty) return -ENOMEM;

    if (!buf_size)
        buf_size = TTY_BUF_SIZE;

    tty->cook = kmalloc(buf_size, &M_TTY_COOK, 0);

    if (!tty->cook) {
        kfree(tty);
        return -ENOMEM;
    }

    tty->size = buf_size;
    tty->pos  = 0;
    tty->col  = 0;
    tty->nl_owed = 0;

    /* Defaults */
    tty->tios.c_iflag = ICRNL | IXON;
    tty->tios.c_oflag = OPOST | ONLCR;
    tty->tios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
    //tty->tios.c_cc[VEOL]   = ;
    tty->tios.c_cc[VERASE] = 0x7f;  /* DEL, what backspace sends */
    tty->tios.c_cc[VEOF]   = 0x04;  /* ^D */
    tty->tios.c_cc[VMIN]   = 1;
    tty->tios.c_cc[VINTR]  = 0x03;  /* ^C */
    tty->tios.c_cc[VKILL]  = 0x15;  /* ^U */
    tty->tios.c_cc[VQUIT]  = 0x1C;  /* ^\ */
    tty->tios.c_cc[VSTART] = 0x11;  /* ^Q */
    tty->tios.c_cc[VSTOP]  = 0x13;  /* ^S */
    tty->tios.c_cc[VSUSP]  = 0x1A;  /* ^Z */

    tty->ws.ws_row = 24;
    tty->ws.ws_col = 80;

    /* Interface */
    tty->master_write = master;
    tty->slave_write  = slave;
    tty->p = p;

    if (ref)
        *ref = tty;

    return 0;
}

/**
 * A terminal device was opened: it becomes the controlling terminal of a
 * session leader that has none (unless O_NOCTTY), if no session has it.
 * Not through /dev/console (as Linux): init writes there, and would take
 * the terminal that is also the console (the first virtual terminal, or
 * the uart) from the session a getty starts on it.
 */
void tty_open(struct tty *tty, struct file *file)
{
    struct session *session = curproc->pgrp->session;
    int console = file->vnode && file->vnode->rdev == DEV(5, 1);

    if ((file->flags & O_NOCTTY) || console || session->leader != curproc ||
        session->ctty || tty->session)
        return;

    session->ctty = tty;
    tty->session  = session;
    tty->fg       = curproc->pgrp;
}

/**
 * The session of a controlling terminal ends (its leader exited) or the
 * terminal goes away: its foreground group gets SIGHUP, and the terminal
 * belongs to no session.
 */
void tty_hangup(struct tty *tty)
{
    if (!tty->session)
        return;

    if (tty->fg) {
        signal_pgrp_send(tty->fg, SIGHUP);
        signal_pgrp_send(tty->fg, SIGCONT);
    }

    tty->session->ctty = NULL;
    tty->session = NULL;
    tty->fg = NULL;
}

/** discard input (TCIFLUSH, TCIOFLUSH): the line being edited, what the
 * driver holds for readers, pending end-of-files; output isn't kept */
void tty_flush(struct tty *tty, int which)
{
    if (which == TCOFLUSH)
        return;

    tty->pos = 0;
    tty->eof = 0;

    if (tty->input_flush)
        tty->input_flush(tty);
}

static void tty_release(struct tty *tty)
{
    kfree(tty->cook);
    kfree(tty);
}

int tty_free(struct tty *tty)
{
    tty_hangup(tty);

    /* writers wait for VSTART: they go, the last frees it */
    if (tty->start_waiters) {
        tty->gone = 1;
        tty->stopped = 0;
        thread_queue_wakeup(&tty->start_wait);
        return 0;
    }

    tty_release(tty);
    return 0;
}
