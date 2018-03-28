#include <core/system.h>
#include <dev/tty.h>

#include <sys/sched.h>  // XXX

ssize_t tty_master_read(struct tty *tty, size_t size, void *buf)
{
    return ringbuf_read(tty->out, size, buf);  
}

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
            } else if (*c == tty->tios.c_cc[VERASE] || *c == '\b') {
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
            } else if (*c == tty->tios.c_cc[VQUIT]) {
            } else if (*c == tty->tios.c_cc[VSTART]) {
            } else if (*c == tty->tios.c_cc[VSUSP]) {
            } else if (*c == '\n' || (*c == '\r' && (tty->tios.c_iflag & ICRNL))) {
                tty->cook[tty->pos++] = '\n';

                if (echo)
                    tty_slave_write(tty, 1, "\n");

                ringbuf_write(tty->in, tty->pos, tty->cook);
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
        return ringbuf_write(tty->in, size, buf);
    }

    return ret;
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
            tty->fg = cur_thread->owner->pgrp;
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

ssize_t tty_slave_read(struct tty *tty, size_t size, void *buf)
{
    return ringbuf_read(tty->in, size, buf);
}

ssize_t tty_slave_write(struct tty *tty, size_t size, void *buf)
{
    if (tty->tios.c_oflag & OPOST) {
        size_t written;
        for (written = 0; written < size; ++written) {
            char c = ((char *) buf)[written];
            if (c == '\n' && (tty->tios.c_oflag & ONLCR)) {
                ringbuf_write(tty->out, 2, "\r\n");
            } else if (c == '\r' && (tty->tios.c_oflag & OCRNL)) {
                ringbuf_write(tty->out, 1, "\n");
            } else if (c == '\r' && (tty->tios.c_oflag & ONOCR)) {
                /* TODO */
            } else if (c == '\n' && (tty->tios.c_oflag & ONLRET)) {
                /* TODO */
            } else {
                ringbuf_write(tty->out, 1, &c);
            }
        }
        return written;
    } else {
        return ringbuf_write(tty->out, size, buf);
    }
}

int tty_new(proc_t *proc, size_t buf_size, struct tty **ref)
{
    struct tty *tty = NULL;
    
    if (!(tty = kmalloc(sizeof(struct tty))))
        return -ENOMEM;

    memset(tty, 0, sizeof(struct tty));

    if (!buf_size)
        buf_size = TTY_BUF_SIZE;

    tty->in   = ringbuf_new(buf_size);
    tty->out  = ringbuf_new(buf_size);
    tty->cook = kmalloc(buf_size);
    tty->pos  = 0;

    /* Defaults */
    tty->tios.c_iflag = ICRNL | IXON;
    tty->tios.c_oflag = OPOST | ONLCR;
    tty->tios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
    //pty->tios.c_cc[VEOL]   = ;
    //pty->tios.c_cc[VERASE] = ;
    tty->tios.c_cc[VINTR]  = 0x03;  /* ^C */
    tty->tios.c_cc[VKILL]  = 0x15;  /* ^U */
    tty->tios.c_cc[VQUIT]  = 0x1C;  /* ^\ */
    tty->tios.c_cc[VSTART] = 0x11;  /* ^Q */
    tty->tios.c_cc[VSUSP]  = 0x1A;  /* ^Z */

    tty->ws.ws_row = 24;
    tty->ws.ws_col = 80;

    tty->fg = proc->pgrp;

    if (ref)
        *ref = tty;

    return 0;
}

int tty_free(struct tty *tty)
{
    kfree(tty->cook);
    ringbuf_free(tty->out);
    ringbuf_free(tty->in);
    kfree(tty);

    return 0;
}
