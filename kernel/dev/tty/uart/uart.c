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

/* Called when data is received */
void uart_recieve_handler(struct uart *u, size_t size)
{
    char buf[UART_BUF];

    for (size_t i = 0; i < size; ++i) {
        buf[i] = u->receive(u);
    }

    tty_master_write(u->tty, size, buf);  
    thread_queue_wakeup(u->vnode->read_queue);
}

/* Called when data is ready to be transmitted */
void uart_transmit_handler(struct uart *u, size_t size)
{
    size_t len = ringbuf_available(u->out);
    len = MIN(size, len);

    for (size_t i = 0; i < len; ++i) {
        char c = 0;
        ringbuf_read(u->out, 1, &c);
        u->transmit(u, c);
    }

    thread_queue_wakeup(u->vnode->write_queue);
}

/* TTY Interface */
ssize_t uart_master_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    size_t s = ringbuf_write(u->out, size, buf);
    uart_transmit_handler(u, s); /* XXX */
    return s;
}

ssize_t uart_slave_write(struct tty *tty, size_t size, void *buf)
{
    struct uart *u = (struct uart *) tty->p;
    return ringbuf_write(u->in, size, buf);
}

#define UART_MAX 192

/* uart minors start at 64 */
static struct uart *uart_get(devid_t minor)
{
    if (minor < 64 || minor - 64 >= UART_MAX)
        return NULL;

    return devices[minor - 64];
}

ssize_t uart_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;
    return ringbuf_read(u->in, size, buf);
}

ssize_t uart_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;
    return tty_slave_write(u->tty, size, buf);
}

int uart_ioctl(struct devid *dd, int request, void *argp)
{
    struct uart *u = uart_get(dd->minor);
    if (!u) return -EIO;
    return tty_ioctl(u->tty, request, argp);  
}

int uart_file_open(struct file *file)
{
    struct uart *u = uart_get(DEV_MINOR(file->vnode->rdev));

    if (!u) /* no uart registered for this minor */
        return -ENXIO;

    if (u->vnode) { /* already open */
        /* XXX */
        file->vnode = u->vnode;
    } else {
        u->init(u);
        u->vnode = file->vnode;
        /* TODO Error checking */
        u->in = ringbuf_new(UART_RING);
        u->out = ringbuf_new(UART_RING);
        tty_new(curproc, 0, uart_master_write, uart_slave_write, u, &u->tty);
        u->tty->vnode = u->vnode;
        file->vnode->read_queue  = queue_new();
        file->vnode->write_queue = queue_new();
    }

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

        .can_write = __vfs_can_always,  /* XXX */
        .eof       = __vfs_eof_never,
    },
};
