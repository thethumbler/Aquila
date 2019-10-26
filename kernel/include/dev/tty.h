#ifndef _DEV_TTY_H
#define _DEV_TTY_H

#include <core/system.h>

struct tty;

#include <fs/termios.h>
#include <fs/ioctl.h>
#include <sys/proc.h>

typedef ssize_t (*ttyio)(struct tty *tty, size_t size, void *buf);

/**
 * \ingroup dev-tty
 * \brief generic tty interface
 */
struct tty {
    /** cooking buffer */
    char *cook;

    /** current position in cooking buffer */
    size_t pos;

    struct termios tios;
    struct winsize ws;

    /** associated device */
    struct dev *dev;

    /** controlling process */
    struct proc *proc;

    /** foreground process group */
    struct pgroup *fg;

    /* interface */

    /** specific handler private data */
    void *p;

    /** master write handelr */
    ttyio   master_write;

    /** slave write handler */
    ttyio   slave_write;
};

#define TTY_BUF_SIZE 512

int     tty_new(struct proc *proc, size_t buf_size, ttyio master, ttyio slave, void *p, struct tty **ref);
int     tty_free(struct tty *tty);
ssize_t tty_master_write(struct tty *tty, size_t size, void *buf);
ssize_t tty_slave_write(struct tty *tty, size_t size, void *buf);
int     tty_ioctl(struct tty *tty, int request, void *argp);

#endif /* ! _DEV_TTY_H */
