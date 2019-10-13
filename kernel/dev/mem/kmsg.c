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

static ssize_t kmsgdev_read(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    //printk("kmsgdev_read(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);
    return ringbuf_read_noconsume(kmsg, offset, size, buf);
}

static ssize_t kmsgdev_write(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    //printk("kmsg: [%d: %d] %s: ", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
    //char *_buf = (char *) buf;
    //size_t sz = size;
    //while (size--)
    //    earlycon_putc(*_buf++);
    //return sz;
    //printk("\n");
    //return ringbuf_write(kmsg, size, buf);

    printk("%s", buf);
    return size;
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
