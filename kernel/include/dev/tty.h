#ifndef _DEV_TTY_H
#define _DEV_TTY_H

#include <core/system.h>

struct tty;

#include <aquila/termios.h>
#include <aquila/ioctl.h>
#include <sys/proc.h>
#include <fs/iobuf.h>

/*
 * Terminals (dev/tty/generic.c): the line discipline that every terminal
 * shares, the virtual terminals, the serial ports and the
 * pseudo-terminals. A terminal has two sides: the master, the device
 * (keyboard and screen, a serial port, a pty's master), and the slave,
 * what programs read and write. See docs/kernel/devices.md.
 */

/* A terminal's write function, for one of its sides. */
typedef ssize_t (*ttyio)(struct tty *tty, size_t size, void *buf);

/** A terminal, and its line discipline's state. */
struct tty {
    /** the line being edited, in canonical mode */
    char *cook;

    /** the size of `cook` */
    size_t size;

    /** how much of `cook` is used */
    size_t pos;

    /** how many end-of-files (VEOF at the start of a line) reads still
     *  have to return */
    int eof;

    /** the output's column (for OPOST's ONOCR and ONLRET), and whether an
     *  ONLCR newline's CR was sent but not its NL yet (the device took no
     *  more) */
    size_t col;
    int nl_owed;

    struct termios tios;
    struct winsize ws;

    /** whether output is stopped (IXON: VSTOP came); writes wait on
     *  `start_wait` for VSTART. `start_waiters` counts them: tty_free()
     *  marks the terminal `gone` and leaves freeing it to the last one */
    int stopped;
    struct waitq start_wait;
    int start_waiters;
    int gone;

    /** the terminal's device */
    struct dev *dev;

    /** the session it is the controlling terminal of, or NULL */
    struct session *session;

    /** the foreground process group, or NULL */
    struct pgroup *fg;

    /** the device's vnode, which /dev/tty opens in its place */
    struct vnode *vnode;

    /* the device's side */

    /** the device's own data */
    void *p;

    /** writes to the master side: input, as if typed */
    ttyio   master_write;

    /** writes to the slave side: output, for the device */
    ttyio   slave_write;

    /** discards the input that readers haven't read yet */
    void    (*input_flush)(struct tty *tty);
};

/* The size of a terminal's line buffer. */
#define TTY_BUF_SIZE 512

/* The line discipline's operations (dev/tty/generic.c). */
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
