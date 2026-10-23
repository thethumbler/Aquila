#include <core/system.h>
#include <dev/dev.h>
#include <fs/posix.h>
#include <ds/ringbuf.h>
#include <sys/sched.h>
#include <console/earlycon.h>

#include <memdev.h>

struct dev kmsgdev;
extern struct ringbuf *kmsg;
extern struct queue *kmsg_wait;

static ssize_t kmsgdev_read(struct devid *dd __unused, off_t offset, struct iobuf *io)
{
    /* the ring from offset on, a piece at a time */
    char tmp[256];
    size_t size = io->resid, done = 0;

    while (done < size) {
        size_t len = ringbuf_read_noconsume(kmsg, offset + done, MIN(size - done, sizeof(tmp)), tmp);

        if (!len)
            break;

        ssize_t r = iobuf_out(io, tmp, len);

        if (r < 0)
            return done ? (ssize_t) done : r;

        done += r;

        if ((size_t) r < len)
            break;
    }

    return done;
}

static ssize_t kmsgdev_write(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    /* the buffer is not NUL-terminated, print exactly what it has */
    char tmp[65];
    size_t size = io->resid, done = 0;

    while (done < size) {
        ssize_t len = iobuf_in(io, tmp, MIN(size - done, sizeof(tmp) - 1));

        if (len < 0)
            return done ? (ssize_t) done : len;

        tmp[len] = '\0';
        printk("%s", tmp);
        done += len;

        if (io->fault)
            break;
    }

    return done;
}

static int kmsg_file_open(struct file *file)
{
    file->vnode->read_queue = kmsg_wait;
    return posix_file_open(file);
}

struct dev kmsgdev = {
    .read  = kmsgdev_read,
    .write = kmsgdev_write,

    .fops  = {
        .open  = kmsg_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .close = posix_file_close,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};
