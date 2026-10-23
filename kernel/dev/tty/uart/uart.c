#include <core/system.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <fs/posix.h>
#include <ds/queue.h>
#include <ds/ringbuf.h>
#include <sys/sched.h>

#define UART_BUF 64

/* must hold a full line from the tty (TTY_BUF_SIZE), like the pty rings */
#define UART_RING 4096
static struct uart *devices[192] = {0};   /* Registered devices */

/** Called when data is received */
void uart_recieve_handler(struct uart *u, size_t size)
{
    char buf[UART_BUF];

    for (size_t i = 0; i < size; ++i) {
        buf[i] = u->receive(u);
    }

    tty_master_write(u->tty, size, buf);
    thread_queue_wakeup(&u->read_queue);
}

/** Called when data is ready to be transmitted */
void uart_transmit_handler(struct uart *u, size_t size)
{
    size_t len = ringbuf_available(u->out);
    len = MIN(size, len);

    for (size_t i = 0; i < len; ++i) {
        char c = 0;
        ringbuf_read(u->out, 1, &c);
        u->transmit(u, c);
    }

    thread_queue_wakeup(&u->write_queue);
}

/** TTY Interface */
ssize_t uart_master_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    size_t s = ringbuf_write(u->out, size, buf);
    uart_transmit_handler(u, s); /* XXX */
    return s;
}

static void uart_input_flush(struct tty *tty)
{
    struct uart *u = (struct uart *) tty->p;
    char c;

    while (ringbuf_read(u->in, 1, &c))
        ;
}

ssize_t uart_slave_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    return ringbuf_write(u->in, size, buf);
}

#define UART_MAX 192

/** uart minors start at 64 */
static struct uart *uart_get(devid_t minor)
{
    if (minor < 64 || minor - 64 >= UART_MAX)
        return NULL;

    return devices[minor - 64];
}

ssize_t uart_read(struct devid *dd, off_t offset __unused, struct iobuf *io)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;

    int err = tty_background(u->tty, SIGTTIN);
    if (err) return err;

    return ringbuf_read_iobuf(u->in, io->resid, io);
}

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

/** a read found no data */
static int uart_file_eof(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return !u || tty_eof(u->tty);
}

static uint64_t uart_read_timeout(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return u ? tty_read_timeout(u->tty) : 0;
}

static int uart_can_read(struct file *file, size_t size)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    return !u || tty_can_read(u->tty, ringbuf_available(u->in));
}

int uart_ioctl(struct devid *dd, int request, void *argp)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;
    return tty_ioctl(u->tty, request, argp);  
}

/** the uart's buffers and terminal, the first time it's opened */
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

    /* the terminal's node (/dev/tty opens it), held: the interrupts and
     * the terminal outlive the node it was opened through */
    u->vnode = vnode;
    vnode->ref++;

    tty->vnode = vnode;
    tty->input_flush = uart_input_flush;
    u->tty = tty;
    return 0;
}

int uart_file_open(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));
    int err;

    if (!u) /* no uart registered for this minor */
        return -ENXIO;

    if (!u->tty && (err = uart_setup(u, file->vnode)))
        return err;

    /* the same queues for every node of it */
    file->vnode->read_queue  = &u->read_queue;
    file->vnode->write_queue = &u->write_queue;

    tty_open(u->tty, file);
    return 0;
}

int uart_register(int id, struct uart *u)
{
    if (id < 0) {   /* Allocated dynamically */
        for (int i = 0; i < 192; ++i) {
            if (!devices[i]) {
                devices[i] = u;
                id = i;
                goto done;
            }
        }

        return -1;  /* Failed */
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
        .can_write = __vfs_can_always,  /* XXX */
        .eof       = uart_file_eof,
        .read_timeout = uart_read_timeout,
    },
};
