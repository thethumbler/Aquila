/** \file
 *          PS/2 Mouse driver
 *
 * /dev/mouse (c 10 1) is the mouse's bytes as they come, as Linux's
 * /dev/psaux: its packets (3 bytes, 4 with a wheel), and its replies (ACK,
 * IDs) to what is written, which goes to the mouse. Readers find where
 * packets start (bit 3 of the first byte); opening it resets the mouse to
 * a plain PS/2 mouse, from the start of a packet.
 */


#include <core/system.h>
#include <core/module.h>
#include <core/arch.h>

#include <cpu/cpu.h>

#include <dev/i8042.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/dev.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <ds/ringbuf.h>
#include <ds/queue.h>

#include <aquila/errno.h>

struct dev ps2mousedev;

#define USE_DEFAULTS        0xF6
#define ENABLE_MOUSE        0xF4
#define RESET               0xFF

#define BUF_SIZE    1024

static struct ringbuf *mouse_ring = RINGBUF_NEW(BUF_SIZE);   /* Mouse ring Buffer */
static struct proc *proc = NULL; /* Current process using mouse */
static struct queue *mouse_read_queue = QUEUE_NEW(); /* Mouse read queue */

/* replies of the reset at open, nobody reads them */
static int skip = 0;

void ps2mouse_handler(int byte)
{
    char b = byte;

    if (skip) {
        skip--;
        return;
    }

    /* lost if nobody reads (the ring is full) */
    ringbuf_write(mouse_ring, 1, &b);

    /* readers, and select() and poll() (which don't sleep on the queue) */
    thread_queue_wakeup(mouse_read_queue);
}

void ps2mouse_register(void)
{
    i8042_handler_register(2, ps2mouse_handler);
}

static ssize_t ps2mouse_read(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    return ringbuf_read_iobuf(mouse_ring, io->resid, io);
}

/** commands to the mouse, it replies (an ACK to each byte, more to some) */
static ssize_t ps2mouse_write(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    size_t size = io->resid;

    for (size_t i = 0; i < size; ++i) {
        uint8_t c;
        ssize_t r = iobuf_in(io, &c, 1);

        if (r < 0)
            return i ? (ssize_t) i : r;

        if (i8042_aux_write(c))
            return i ? (ssize_t) i : -EIO;
    }

    return size;
}

/** its defaults, reporting (the controller's second port is on: the
 * i8042's driver); its replies (ACKs) come before anyone reads, and
 * opening it does the same again */
int ps2mouse_probe()
{
    i8042_aux_write(USE_DEFAULTS);
    i8042_aux_write(ENABLE_MOUSE);

    ps2mouse_register();
    kdev_chrdev_register(10, &ps2mousedev);

    return 0;
}

/* File Operations */

static int ps2mouse_file_open(struct file *file)
{
    if (proc) /* Only one process can open kbd */
        return -EACCES;

    proc = curproc;
    file->vnode->read_queue = mouse_read_queue;    

    /* a plain PS/2 mouse, whatever the last opener made it (its replies,
     * ACK AA 00 and ACK, aren't read), and nothing left of before */
    char c;
    while (ringbuf_read(mouse_ring, 1, &c))
        ;
    skip = 4;
    i8042_aux_write(RESET);
    i8042_aux_write(ENABLE_MOUSE);

    return posix_file_open(file);
}

static int ps2mouse_file_can_read(struct file *file, size_t size)
{
    (void) file;
    return ringbuf_available(mouse_ring);
}

static int ps2mouse_file_close(struct file *file)
{
    //printk("ps2mouse_file_close(file=%p)\n", file);
    proc = NULL;
    return posix_file_close(file);
}

struct dev ps2mousedev = {
    .name  = "mousedev",

    .probe = ps2mouse_probe,
    .read  = ps2mouse_read,
    .write = ps2mouse_write,

    .fops  = {
        .open  = ps2mouse_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .close = ps2mouse_file_close,

        .can_read  = ps2mouse_file_can_read,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};

MODULE_INIT(ps2mouse, ps2mouse_probe, NULL)
