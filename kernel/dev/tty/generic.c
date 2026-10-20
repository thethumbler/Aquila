#include <core/system.h>
#include <sys/sched.h>
#include <dev/tty.h>
#include <sys/uaccess.h>
#include <sys/signal.h>
#include <aquila/fcntl.h>

MALLOC_DEFINE(M_TTY, "tty", "tty structure");
MALLOC_DEFINE(M_TTY_COOK, "tty-cook", "tty cooking buffer");

ssize_t tty_master_write(struct tty *tty, size_t size, void *buf)
{
    ssize_t ret = size;

    /* Process Slave Input */
    if (tty->tios.c_lflag & ICANON) {   /* Canonical mode */
        int echo = tty->tios.c_lflag & ECHO;
        char *c = buf;

        while (size) {
            if (*c == tty->tios.c_cc[VEOF]) {
                /* TODO */
            } else if (*c == tty->tios.c_cc[VEOL]) {
                /* TODO */
            } else if (*c == tty->tios.c_cc[VERASE]) {
                /* The ERASE character shall delete the last character in
                 * the current line, if there is one.
                 */
                if (tty->pos > 0) {
                    --tty->pos;
                    tty->cook[tty->pos] = '\0';

                    if (tty->tios.c_lflag & ECHOE)
                        tty_slave_write(tty, 3, "\b \b");
                }

                goto skip_echo;
            } else if (*c == tty->tios.c_cc[VINTR]) {
                if (tty->fg)
                    signal_pgrp_send(tty->fg, SIGINT);
                char cc[] = {'^', *c + '@', '\n'};
                tty_slave_write(tty, 3, cc);
                goto skip_echo;
            } else if (*c == tty->tios.c_cc[VKILL]) {
                /* The KILL character shall delete all data in the current
                 * line, if there is any.
                 */
            } else if (*c == tty->tios.c_cc[VQUIT]) {
            } else if (*c == tty->tios.c_cc[VSTART]) {
            } else if (*c == tty->tios.c_cc[VSUSP]) {
            } else if (*c == '\n' ||
                    (*c == '\r' && (tty->tios.c_iflag & ICRNL))) {
                tty->cook[tty->pos++] = '\n';

                if (echo)
                    tty_slave_write(tty, 1, "\n");

                tty->slave_write(tty, tty->pos, tty->cook);

                tty->pos = 0;
                ret = ret - size + 1;
                return ret;
            } else {
                /* Always keep room for the terminating '\n', drop
                 * input that does not fit in the cooking buffer
                 */
                if (tty->pos + 1 >= tty->size)
                    goto skip_echo;

                tty->cook[tty->pos++] = *c;
            }

            if (echo) {
                if (*c < ' ') { /* Non-printable */
                    char cc[] = {'^', *c + '@'};
                    tty_slave_write(tty, 2, cc);
                } else {
                    tty_slave_write(tty, 1, c);
                }
            }
skip_echo:
            ++c;
            --size;
        }
    } else {
        return tty->slave_write(tty, size, buf);
    }

    return ret;
}

ssize_t tty_slave_write(struct tty *tty, size_t size, void *_buf)
{
    const char *buf = (const char *) _buf;

    if (tty->tios.c_oflag & OPOST) {
        size_t written = 0;

        for (written = 0; written < size; ++written) {
            char c = buf[written];

            if (c == '\n' && (tty->tios.c_oflag & ONLCR)) {
                /* If ONLCR is set, the NL character shall
                 * be transmitted as the CR-NL character pair. 
                 */
                if (tty->master_write(tty, 2, "\r\n") != 2)
                    /* FIXME should handle special cases */
                    break;
            } else if (c == '\r' && (tty->tios.c_oflag & OCRNL)) {
                /* If OCRNL is set, the CR character shall
                 * be transmitted as the NL character.
                 */
                if (tty->master_write(tty, 1, "\n") != 1)
                    break;
            } else if (c == '\r' && (tty->tios.c_oflag & ONOCR)) {
                /* If ONOCR is set, no CR character shall be
                 * transmitted when at column 0 (first position)
                 */
                /* FIXME should track the output column, not the
                 * cooking buffer position
                 */
                if (tty->ws.ws_row && (tty->pos % tty->ws.ws_row))
                    if (tty->master_write(tty, 1, &c) != 1)
                        break;
            } else if (c == '\n' && (tty->tios.c_oflag & ONLRET)) {
                /* If ONLRET is set, the NL character is assumed
                 * to do the carriage-return function; the column
                 * pointer shall be set to 0 and the delays specified
                 * for CR shall be used. Otherwise, the NL character is
                 * assumed to do just the line-feed function; the column
                 * pointer remains unchanged. The column pointer shall also
                 * be set to 0 if the CR character is actually transmitted.
                 */

                /* TODO */
            } else {
                if (tty->master_write(tty, 1, &c) != 1)
                    break;
            }
        }
        return written;
    } else {
        return tty->master_write(tty, size, _buf);
    }
}

/** size of the user buffer an ioctl request reads or writes */
static size_t tty_ioctl_argsize(int request)
{
    switch (request) {
        case TCGETS:
        case TCSETS:
            return sizeof(struct termios);
        case TIOCGPGRP:
        case TIOCSPGRP:
            return sizeof(pid_t);
        case TIOCGWINSZ:
        case TIOCSWINSZ:
            return sizeof(struct winsize);
    }

    return 0;
}

int tty_ioctl(struct tty *tty, int request, void *argp)
{
    /* argp comes straight from user space */
    if (uaccess_check(argp, tty_ioctl_argsize(request)))
        return -EFAULT;

    switch (request) {
        case TCGETS:
            memcpy(argp, &tty->tios, sizeof(struct termios));
            break;
        case TCSETS:
            memcpy(&tty->tios, argp, sizeof(struct termios));
            break;
        case TIOCGPGRP:
            /* without one, a value that is no process group (POSIX) */
            *(pid_t *) argp = tty->fg ? tty->fg->pgid : 0x7fffffff;
            break;
        case TIOCSPGRP: {
            /* the caller's controlling terminal, a group of its session */
            pid_t pgid = *(pid_t *) argp;
            struct session *session = curproc->pgrp->session;
            struct pgroup *pgrp = NULL;

            if (session->ctty != tty)
                return -ENOTTY;

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
            memcpy(argp, &tty->ws, sizeof(struct winsize));
            break;
        case TIOCSWINSZ:
            memcpy(&tty->ws, argp, sizeof(struct winsize));
            break;
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
            return -EINVAL;
    }
    
    return 0;
}

/**
 * \ingroup dev-tty
 * \brief create a new generic tty interface
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

    /* Defaults */
    tty->tios.c_iflag = ICRNL | IXON;
    tty->tios.c_oflag = OPOST | ONLCR;
    tty->tios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
    //tty->tios.c_cc[VEOL]   = ;
    tty->tios.c_cc[VERASE] = 0x08;  /* BS */
    tty->tios.c_cc[VINTR]  = 0x03;  /* ^C */
    tty->tios.c_cc[VKILL]  = 0x15;  /* ^U */
    tty->tios.c_cc[VQUIT]  = 0x1C;  /* ^\ */
    tty->tios.c_cc[VSTART] = 0x11;  /* ^Q */
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

/*
 * A terminal device was opened: it becomes the controlling terminal of a
 * session leader that has none (unless O_NOCTTY), if no session has it.
 */
void tty_open(struct tty *tty, struct file *file)
{
    struct session *session = curproc->pgrp->session;

    if ((file->flags & O_NOCTTY) || session->leader != curproc ||
        session->ctty || tty->session)
        return;

    session->ctty = tty;
    tty->session  = session;
    tty->fg       = curproc->pgrp;
}

/*
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

int tty_free(struct tty *tty)
{
    tty_hangup(tty);

    kfree(tty->cook);
    kfree(tty);

    return 0;
}
