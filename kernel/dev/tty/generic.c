#include <core/system.h>
#include <sys/sched.h>
#include <dev/tty.h>

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
            } else if (*c == tty->tios.c_cc[VEOL]) {
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
                if (tty->pos % tty->ws.ws_row)
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

int tty_ioctl(struct tty *tty, int request, void *argp)
{
    switch (request) {
        case TCGETS:
            memcpy(argp, &tty->tios, sizeof(struct termios));
            break;
        case TCSETS:
            memcpy(&tty->tios, argp, sizeof(struct termios));
            break;
        case TIOCGPGRP:
            *(pid_t *) argp = tty->fg->pgid;
            break;
        case TIOCSPGRP:
            /* XXX */
            tty->fg = curproc->pgrp;
            break;
        case TIOCGWINSZ:
            memcpy(argp, &tty->ws, sizeof(struct winsize));
            break;
        case TIOCSWINSZ:
            memcpy(&tty->ws, argp, sizeof(struct winsize));
            break;
        default:
            return -1;
    }
    
    return 0;
}

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

    tty->fg = proc->pgrp;

    /* Interface */
    tty->master_write = master;
    tty->slave_write  = slave;
    tty->p = p;

    if (ref)
        *ref = tty;

    return 0;
}

int tty_free(struct tty *tty)
{
    kfree(tty->cook);
    kfree(tty);

    return 0;
}
