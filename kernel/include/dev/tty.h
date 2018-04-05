#ifndef _TTY_H
#define _TTY_H

#include <core/system.h>
#include <fs/termios.h>
#include <fs/ioctl.h>
#include <sys/proc.h>

struct tty;
typedef ssize_t (*ttyio)(struct tty *tty, size_t size, void *buf);

struct tty {
    char   *cook;     /* Cooking buffer */
    size_t pos;

    struct termios tios;
    struct winsize ws;

    proc_t   *proc;   /* Controlling Process */
    pgroup_t *fg;     /* Foreground Process Group */

    /* Interface */
    void    *p;       /* Private data */
    ttyio   master_write;
    ttyio   slave_write;
};

#define TTY_BUF_SIZE 512

int     tty_new(proc_t *proc, size_t buf_size, ttyio master, ttyio slave, void *p, struct tty **ref);
int     tty_free(struct tty *tty);
ssize_t tty_master_write(struct tty *tty, size_t size, void *buf);
ssize_t tty_slave_write(struct tty *tty, size_t size, void *buf);
int     tty_ioctl(struct tty *tty, int request, void *argp);

#endif /* !_TTY_H */
