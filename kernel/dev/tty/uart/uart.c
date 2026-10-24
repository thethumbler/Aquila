/** \file
 * Serial ports as terminals: `/dev/ttyS0` and up (character device 4, 64
 * and up).
 *
 * A UART driver (8250, PL011) registers its UARTs here, and this layer
 * makes each one a terminal: it connects the UART to the terminal layer
 * (`generic.c`), which does the line editing, with a ring each way. What
 * the UART receives goes to the terminal; what the terminal outputs goes
 * to the UART. A UART is set up the first time it is opened.
 */

#include <core/system.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <fs/posix.h>
#include <ds/queue.h>
#include <ds/ringbuf.h>
#include <sys/sched.h>

#define UART_BUF 64

/* each ring's size: it must hold a full line from the terminal
 * (TTY_BUF_SIZE), like the pseudo-terminals' rings */
#define UART_RING 4096
static struct uart *devices[192] = {0};   /* the registered UARTs, by number */

/**
 * Takes characters a UART received, and gives them to its terminal. The
 * UART's driver calls it from its interrupt handler.
 *
 * @param u     the UART
 * @param size  how many characters to take from it
 */
void uart_receive_handler(struct uart *u, size_t size)
{
    char buf[UART_BUF];

    /* take them a buffer at a time */
    while (size) {
        size_t n = MIN(size, sizeof(buf));

        for (size_t i = 0; i < n; ++i)
            buf[i] = u->receive(u);

        tty_master_write(u->tty, n, buf);
        size -= n;
    }

    waitq_wakeup(&u->read_queue);
}

/**
 * Sends characters that the terminal output to its UART.
 *
 * @param u     the UART
 * @param size  how many characters to send, at most
 */
void uart_transmit_handler(struct uart *u, size_t size)
{
    size_t len = ringbuf_available(u->out);
    len = MIN(size, len);

    for (size_t i = 0; i < len; ++i) {
        char c = 0;
        ringbuf_read(u->out, 1, &c);
        u->transmit(u, c);
    }

    waitq_wakeup(&u->write_queue);
}

/**
 * Takes the terminal's output, and sends it to the UART at once: the UART
 * drivers don't use a "transmitter empty" interrupt.
 *
 * @param tty   the terminal
 * @param size  the output's size, in bytes
 * @param buf   the output
 * @return how much of it was taken
 */
ssize_t uart_master_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    size_t s = ringbuf_write(u->out, size, buf);
    uart_transmit_handler(u, s);
    return s;
}

/** Drops the input waiting to be read (`tcflush()`). */
static void uart_input_flush(struct tty *tty)
{
    struct uart *u = (struct uart *) tty->p;
    char c;

    while (ringbuf_read(u->in, 1, &c))
        ;
}

/**
 * Takes the terminal's input, after its line editing, for the readers of
 * the UART's device.
 *
 * @param tty   the terminal
 * @param size  the input's size, in bytes
 * @param buf   the input
 * @return how much of it was taken
 */
ssize_t uart_slave_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    return ringbuf_write(u->in, size, buf);
}

#define UART_MAX 192

/** Returns the UART of a minor (they start at 64), or NULL if there's
 * none. Without virtual terminals, minor 1 (`/dev/console`) is the first
 * UART too. */
static struct uart *uart_get(devid_t minor)
{
#ifndef CONFIG_DEV_CONSOLE
    /* no virtual terminals: /dev/console (5, 1) is the first UART */
    if (minor == 1)
        minor = 64;
#endif

    if (minor < 64 || minor - 64 >= UART_MAX)
        return NULL;

    return devices[minor - 64];
}

/**
 * Reads a UART's terminal. A process of a background group gets
 * `SIGTTIN` instead.
 *
 * @param dd      the device's number
 * @param offset  not used: a terminal has no offset
 * @param io      where to put what is read
 * @return how much was read, or a negative errno
 */
ssize_t uart_read(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;

    int err = tty_background(u->tty, SIGTTIN);
    if (err) return err;

    return ringbuf_read_iobuf(u->in, io->resid, io);
}

/**
 * Writes to a UART's terminal. With `TOSTOP` set, a process of a
 * background group gets `SIGTTOU` instead.
 *
 * @param dd      the device's number
 * @param offset  not used: a terminal has no offset
 * @param io      what to write
 * @return how much was written, or a negative errno
 */
ssize_t uart_write(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;

    if (u->tty->tios.c_lflag & TOSTOP) {
        int err = tty_background(u->tty, SIGTTOU);

        if (err)
            return err;
    }

    return tty_write_iobuf(u->tty, io, tty_slave_write);
}

/** Returns whether a read that found no data is at the end of the file
 * (the terminal says, from its mode). */
static int uart_file_eof(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return !u || tty_eof(u->tty);
}

/** Returns how long a read may wait for data, from the terminal's mode
 * (`VTIME`), or 0 for no limit. */
static uint64_t uart_read_timeout(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return u ? tty_read_timeout(u->tty) : 0;
}

/** Returns whether a read wouldn't have to wait. */
static int uart_can_read(struct file *file, size_t size)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return !u || tty_can_read(u->tty, ringbuf_available(u->in));
}

/**
 * Handles a UART terminal's `ioctl()`s, which are the terminal's.
 *
 * @param dd       the device's number
 * @param request  the request
 * @param argp     its argument
 * @return what the request returns, or a negative errno
 */
int uart_ioctl(struct devid *dd, int request, void *argp)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;
    return tty_ioctl(u->tty, request, argp);  
}

/** Sets a UART up the first time it is opened: its rings, its terminal,
 * and the UART itself. Returns 0 or a negative errno. */
static int uart_setup(struct uart *u, struct vnode *vnode)
{
    struct ringbuf *in = ringbuf_new(UART_RING), *out = ringbuf_new(UART_RING);
    struct tty *tty = NULL;
    int err = -ENOMEM;

    if (!in || !out || (err = tty_new(curproc, 0, uart_master_write, uart_slave_write, u, &tty))) {
        ringbuf_free(in);
        ringbuf_free(out);
        return err;
    }

    u->init(u);
    u->in = in;
    u->out = out;

    /* keep the node it was opened through (/dev/tty opens it): the
     * interrupts and the terminal outlive that file */
    u->vnode = vnode;
    vnode->ref++;

    tty->vnode = vnode;
    tty->input_flush = uart_input_flush;
    u->tty = tty;
    return 0;
}

/**
 * Opens a UART's device, setting the UART up the first time.
 *
 * @param file  the file being opened
 * @return 0, `-ENXIO` if no UART has that minor, or a negative errno
 */
int uart_file_open(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    int err;

    if (!u) /* no UART has this minor */
        return -ENXIO;

    if (!u->tty && (err = uart_setup(u, file->vnode)))
        return err;

    /* every node of the UART shares the same wait queues */
    file->vnode->read_queue  = &u->read_queue;
    file->vnode->write_queue = &u->write_queue;

    tty_open(u->tty, file);
    return 0;
}

/**
 * Registers a UART, as `/dev/ttyS<id>`.
 *
 * @param id  its number, or -1 for the first free one
 * @param u   the UART
 * @return its number, or `-ENOSPC` if every number is taken
 */
int uart_register(int id, struct uart *u)
{
    if (id < 0) {   /* the first free number */
        for (int i = 0; i < 192; ++i) {
            if (!devices[i]) {
                devices[i] = u;
                id = i;
                goto done;
            }
        }

        return -ENOSPC;     /* every minor taken */
    }

done:
    devices[id] = u;

    printk("uart: registered uart %d: %s\n", id, u->name);
    return id;
}

struct dev uart = {
    .name = "uart",

    .read  = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl,

    .fops = {
        .open  = uart_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = uart_can_read,
        .can_write = __vfs_can_always,  /* a writer never waits */
        .eof       = uart_file_eof,
        .read_timeout = uart_read_timeout,
    },
};
