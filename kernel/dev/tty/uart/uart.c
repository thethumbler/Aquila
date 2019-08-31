#include <core/system.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <dev/uart.h>
#include <fs/posix.h>
#include <ds/queue.h>
#include <ds/ringbuf.h>
#include <sys/sched.h>

#define UART_BUF 64
static struct uart *devices[192] = {0};   /* Registered devices */

/* Called when data is received */
void uart_recieve_handler(struct uart *u, size_t size)
{
    char buf[UART_BUF];

    for (size_t i = 0; i < size; ++i) {
        buf[i] = u->receive(u);
    }

    tty_master_write(u->tty, size, buf);  
    thread_queue_wakeup(u->inode->read_queue);
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

    thread_queue_wakeup(u->inode->write_queue);
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

ssize_t uart_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct uart *u = devices[dd->minor - 64];
    if (!u) return -EIO;
    return ringbuf_read(u->in, size, buf);
}

ssize_t uart_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct uart *u = devices[dd->minor - 64];
    if (!u) return -EIO;
    return tty_slave_write(u->tty, size, buf);
}

int uart_ioctl(struct devid *dd, int request, void *argp)
{
    struct uart *u = devices[dd->minor - 64];
    if (!u) return -EIO;
    return tty_ioctl(u->tty, request, argp);  
}

int uart_file_open(struct file *file)
{
    size_t id = (file->inode->rdev & 0xFF) - 64;
    struct uart *u = devices[id];
    int err = 0;

    if (u->inode) { /* Already open */
        /* XXX */
        file->inode = u->inode;
    } else {
        u->init(u);
        u->inode = file->inode;
        /* TODO Error checking */
        u->in = ringbuf_new(UART_BUF);
        u->out = ringbuf_new(UART_BUF);
        tty_new(cur_thread->owner, 0, uart_master_write, uart_slave_write, u, &u->tty);
        file->inode->read_queue  = queue_new();
        file->inode->write_queue = queue_new();
    }

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
