#include <core/system.h>
#include <core/module.h>
#include <sys/sched.h>
#include <dev/dev.h>
#include <ds/ringbuf.h>
#include <ds/queue.h>

#include <bits/errno.h>

#define BUFSIZE    128

struct keyboard keyboards[4];

static struct proc *proc = NULL; /* Current process using Keboard */

static void ps2kbd_handler(int scancode)
{
    ringbuf_write(kbd_ring, sizeof(scancode), (char *) &scancode);
    
    if (kbd_read_queue->count)
        thread_queue_wakeup(kbd_read_queue);
}

static void ps2kbd_register(void)
{
    x86_i8042_handler_register(1, ps2kbd_handler);
}

static ssize_t kbddev_read(struct devid *dd, off_t off, size_t sz, void *buf)
{
    (void) off;

    int id = dd->minor;

    if (id >= MAX_KEYBOARDS)
        return -ENXIO;

    if (keyboards[id].ring) {
        ssize_t ret = ringbuf_write(kbd_ring, sizeof(scancode), (char *) &scancode);
        
        if (keyboards[id].read_queue->count)
            thread_queue_wakeup(keyboards[id].read_queue);

        return ret;
    }

    return -ENXIO;
}

static ssize_t kbddev_write(struct devid *dd, off_t off, size_t sz, void *buf)
{
    (void) off;

    int id = dd->minor;

    if (id >= MAX_KEYBOARDS)
        return -ENXIO;

    if (keyboards[id].ring)
        return ringbuf_read(keyboards[id].ring, sz, buf);

    return -ENXIO;
}

static int kbddev_probe(void)
{
    kdev_chrdev_register(11, &kbddev);
    return 0;
}

/* File Operations */
static int kbddev_file_open(struct file *file)
{
    if (proc) /* Only one process can open kbd */
        return -EACCES;

    proc = cur_thread->owner;

    /* This is either really smart or really dumb */
    file->inode->read_queue = keyboards[0].read_queue;    

    return posix_file_open(file);
}

int keyboard_register(struct keyboard *keyboard)
{
}

struct dev kbddev = {
    .name  = "kbddev",

    .probe = kbddev_probe,
    .read  = kbddev_read,

    .fops  = {
        .open  = kbddev_file_open,
        .read  = posix_file_read,

        .can_read  = __always,
        .can_write = __never,
        .eof       = __never,
    },
};

MODULE_INIT(kbddev, kbddev_probe, NULL)
