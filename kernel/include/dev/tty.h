#ifndef _TTY_H
#define _TTY_H

#include <ds/ring.h>
#include <fs/termios.h>
#include <fs/ioctl.h>
#include <sys/proc.h>

struct tty {
    struct ringbuf *in;    /* Slave Input, Master Output */
    struct ringbuf *out;   /* Slave Output, Master Input */

    char   *cook;     /* Cooking buffer */
    size_t pos;

    struct termios tios;
    struct winsize ws;

    proc_t   *proc;   /* Controlling Process */
    pgroup_t *fg;     /* Foreground Process Group */
};

#define TTY_BUF_SIZE 512


int     tty_new(proc_t *proc, size_t buf_size, struct tty **ref);
int     tty_free(struct tty *tty);
ssize_t tty_master_read(struct tty *tty, size_t size, void *buf);
ssize_t tty_master_write(struct tty *tty, size_t size, void *buf);
ssize_t tty_slave_read(struct tty *tty, size_t size, void *buf);
ssize_t tty_slave_write(struct tty *tty, size_t size, void *buf);
int tty_ioctl(struct tty *tty, int request, void *argp);

#endif /* !_TTY_H */
