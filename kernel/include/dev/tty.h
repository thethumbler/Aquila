#ifndef _DEV_TTY_H
#define _DEV_TTY_H

#include <core/system.h>

struct tty;

#include <aquila/termios.h>
#include <aquila/ioctl.h>
#include <sys/proc.h>
#include <fs/iobuf.h>

typedef ssize_t (*ttyio)(struct tty *tty, size_t size, void *buf);

/**
 * generic tty interface
 */
struct tty {
    /** cooking buffer */
    char *cook;

    /** size of cooking buffer */
    size_t size;

    /** current position in cooking buffer */
    size_t pos;

    /** end-of-files (VEOF at the start of a line) reads have to return */
    int eof;

    /** the output's column (OPOST: ONOCR, ONLRET), and an ONLCR newline
     *  whose CR went but not its NL (the device took no more) */
    size_t col;
    int nl_owed;

    struct termios tios;
    struct winsize ws;

    /** associated device */
    struct dev *dev;

    /** the session it is the controlling terminal of, NULL if none */
    struct session *session;

    /** foreground process group, NULL if none */
    struct pgroup *fg;

    /** the device's vnode, which /dev/tty opens instead of itself */
    struct vnode *vnode;

    /* interface */

    /** specific handler private data */
    void *p;

    /** master write handelr */
    ttyio   master_write;

    /** slave write handler */
    ttyio   slave_write;

    /** discards the input readers haven't read yet */
    void    (*input_flush)(struct tty *tty);
};

#define TTY_BUF_SIZE 512

int     tty_new(struct proc *proc, size_t buf_size, ttyio master, ttyio slave, void *p, struct tty **ref);
int     tty_free(struct tty *tty);
ssize_t tty_master_write(struct tty *tty, size_t size, void *buf);
ssize_t tty_slave_write(struct tty *tty, size_t size, void *buf);
ssize_t tty_write_iobuf(struct tty *tty, struct iobuf *io, ttyio write);
int     tty_ioctl(struct tty *tty, int request, void *argp);
void    tty_open(struct tty *tty, struct file *file);
void    tty_hangup(struct tty *tty);
void    tty_flush(struct tty *tty, int which);
int     tty_eof(struct tty *tty);
uint64_t tty_read_timeout(struct tty *tty);
int     tty_can_read(struct tty *tty, size_t buffered);
int     tty_background(struct tty *tty, int sig);

#endif /* ! _DEV_TTY_H */
