/** \file
 * The PS/2 mouse driver: `/dev/mouse` (character device 10, 1).
 *
 * As Linux's `/dev/psaux`, reading it gives the mouse's bytes as they
 * come: its movement packets (3 bytes, or 4 with a wheel), and its
 * replies (an ACK, its ID) to the commands written to it, which go to the
 * mouse. A reader finds where packets start by bit 3 of their first byte.
 * Opening it resets the mouse to a plain PS/2 mouse, so the first byte
 * read starts a packet. The mouse is on the second port of the i8042
 * controller (`dev/input/i8042.c`).
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

static struct ringbuf *mouse_ring = RINGBUF_NEW(BUF_SIZE);   /* the bytes received */
static struct proc *proc = NULL;    /* the process that has it open */
static struct waitq *mouse_read_queue = &(struct waitq){0};  /* readers sleep here */

/* how many of the replies to the reset done at open are still to come:
 * they are dropped, nobody reads them */
static int skip = 0;

/**
 * Takes a byte from the mouse, which the i8042 driver received: it goes
 * to the readers, unless it is a reply to the reset done at open.
 *
 * @param byte  the byte
 */
void ps2mouse_handler(int byte)
{
    char b = byte;

    if (skip) {
        skip--;
        return;
    }

    /* if nobody reads and the ring is full, the byte is lost */
    ringbuf_write(mouse_ring, 1, &b);

    /* wake the readers, and select() and poll(), which don't sleep on
     * the queue */
    waitq_wakeup(mouse_read_queue);
}

/** Has the i8042 driver send the bytes of its second port, the mouse's,
 * to this driver. */
void ps2mouse_register(void)
{
    i8042_handler_register(2, ps2mouse_handler);
}

/** Reads the bytes received from the mouse. */
static ssize_t ps2mouse_read(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    return ringbuf_read_iobuf(mouse_ring, io->resid, io);
}

/** Sends what is written to the mouse, a byte at a time: commands, which
 * it replies to (an ACK to each byte, and more to some). */
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

/**
 * Sets the mouse up at boot (its default settings, and reporting on) and
 * registers `/dev/mouse`. The i8042 driver turned the controller's second
 * port on. The mouse's replies come before anyone reads, and opening the
 * device sets the mouse up again anyway.
 *
 * @return 0
 */
int ps2mouse_probe()
{
    i8042_aux_write(USE_DEFAULTS);
    i8042_aux_write(ENABLE_MOUSE);

    ps2mouse_register();
    kdev_chrdev_register(10, &ps2mousedev);

    return 0;
}

/** Opens `/dev/mouse`, which one process at a time may have open
 * (`EACCES` otherwise), and resets the mouse. */
static int ps2mouse_file_open(struct file *file)
{
    if (proc) /* only one process can have the mouse open */
        return -EACCES;

    proc = curproc;
    file->vnode->read_queue = mouse_read_queue;    

    /* make it a plain PS/2 mouse again, whatever the last opener made it
     * (its replies, ACK AA 00 and ACK, are dropped), with nothing left
     * from before in the ring */
    char c;
    while (ringbuf_read(mouse_ring, 1, &c))
        ;
    skip = 4;
    i8042_aux_write(RESET);
    i8042_aux_write(ENABLE_MOUSE);

    return posix_file_open(file);
}

/** Returns whether there are bytes to read. */
static int ps2mouse_file_can_read(struct file *file, size_t size)
{
    (void) file;
    return ringbuf_available(mouse_ring);
}

/** Closes `/dev/mouse`: another process may open it now. */
static int ps2mouse_file_close(struct file *file)
{
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
