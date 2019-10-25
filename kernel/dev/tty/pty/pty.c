#include <core/system.h>
#include <core/module.h>
#include <fs/vfs.h>
#include <fs/devpts.h>
#include <fs/posix.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <sys/sched.h>
#include <bits/errno.h>
#include <ds/ringbuf.h>

MALLOC_DEFINE(M_PTY, "pseudo-terminal", "pseudo-terminal structure");

/**
 * \ingroup dev-tty
 * \brief psuedo-terminal
 */
struct pty {
    size_t id;
    struct tty *tty;

    struct vnode *master;
    struct vnode *slave;

    /** master input, slave output */
    struct ringbuf *in;

    /** slave input, master output */
    struct ringbuf *out;

    /** slave read, master write wait queue */
    struct queue pts_read_queue;

    /** slave write, master read wait queue */
    struct queue pts_write_queue;
};

#define PTY_BUF 4096

static struct pty *ptys[256] = {0};

struct dev ptmdev;
struct dev ptsdev;

/* TTY Interface */
ssize_t pty_master_write(struct tty *tty, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) tty->p;
    size_t written = 0;

    while (written < size) {
        written += ringbuf_write(pty->out, size - written, (char *) buf + written);

        if (written != size) {
            /* writes must always complete */
            if (thread_queue_sleep(&pty->pts_read_queue))
                return -EINTR;
        }
    }

    return written;
}

ssize_t pty_slave_write(struct tty *tty, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) tty->p;
    size_t written = 0;

    while (written < size) {
        written += ringbuf_write(pty->in, size - written, (char *) buf + written);

        if (written != size) {
            /* writes must always complete */
            if (thread_queue_sleep(&pty->pts_read_queue))
                return -EINTR;
        }
    }

    return written;
}

/*************************************
 *
 * Pseudo Terminal Master Helpers
 *
 *************************************/

static int ptm_new(struct pty *pty, struct vnode **ref)
{
    /* anonymous file, not populated */
    struct vnode *ptm = NULL;
    
    ptm = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (ptm == NULL) return -ENOMEM;

    ptm->rdev        = DEV(2, pty->id);
    ptm->mode        = S_IFCHR;
    ptm->read_queue  = &pty->pts_read_queue;
    ptm->write_queue = &pty->pts_write_queue;
    ptm->p           = pty;

    if (ref)
        *ref = ptm;

    return 0;
}

static ssize_t ptm_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = ptys[dd->minor];
    size_t ret = ringbuf_read(pty->out, size, buf);  

    thread_queue_wakeup(&pty->pts_read_queue);

    return ret;
}

static ssize_t ptm_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = ptys[dd->minor];
    return tty_master_write(pty->tty, size, buf);  
}

static int pty_ioctl(struct devid *dd, int request, void *argp)
{
    struct pty *pty = ptys[dd->minor];

    switch (request) {
        case TIOCGPTN:
            *(int *) argp = pty->id;
            return 0;
        case TIOCSPTLCK:
            *(int *) argp = 0;  /* FIXME */
            return 0;
    }

    return tty_ioctl(pty->tty, request, argp);  
}

/*************************************
 *
 * Pseudo Terminal Slave Helpers
 *
 *************************************/

static int pts_new(struct pty *pty, struct vnode **ref)
{
    char name[12];
    memset(name, 0, 12);
    snprintf(name, 11, "%d", pty->id);

    dev_t dev = DEV(136 + pty->id / 256, pty->id % 256);

    struct uio uio = {0};

    struct vnode *pts = NULL;
    int err = 0;
    if ((err = vfs_vmknod(devpts_root, name, S_IFCHR, dev, &uio, &pts)))
        return err;

    pts->read_queue = &pty->pts_read_queue;
    pts->write_queue = &pty->pts_write_queue;

    if (ref)
        *ref = pts;

    return 0;
}

ssize_t pts_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    /* FIXME check bounds */
    struct pty *pty = ptys[dd->minor];
    return ringbuf_read(pty->in, size, buf);
}

ssize_t pts_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    /* FIXME check bounds */
    struct pty *pty = ptys[dd->minor];
    return tty_slave_write(pty->tty, size, buf);
}

int pts_can_read(struct file *file, size_t size)
{
    struct devid *dd = &VNODE_DEV(file->vnode);
    struct pty *pty = ptys[dd->minor];
    return ringbuf_available(pty->in);
}

/*************************************
 *
 * Pseudo Terminal Helpers
 *
 *************************************/

static inline size_t pty_id_get(void)
{
    /* XXX */
    static size_t id = 0;
    return id++;
}

int pty_new(struct proc *proc, struct vnode **master)
{
    int err = 0;
    struct pty *pty = NULL;

    pty = kmalloc(sizeof(struct pty), &M_PTY, M_ZERO);
    if (!pty) goto e_nomem;

    pty->id = pty_id_get();

    if (!(pty->in = ringbuf_new(PTY_BUF))) {
        err = -ENOMEM;
        goto error;
    }

    if (!(pty->out = ringbuf_new(PTY_BUF))) {
        err = -ENOMEM;
        goto error;
    }

    if ((err = tty_new(proc, 0, pty_master_write, pty_slave_write, pty, &pty->tty)))
        goto error;

    pty->tty->dev = &ptmdev;

    if ((err = ptm_new(pty, &pty->master)))
        goto error;

    if (master)
        *master = pty->master;
    
    if ((err = pts_new(pty, &pty->slave)))
        goto error;

    ptys[pty->id] = pty;

    return 0;

e_nomem:
    err = -ENOMEM;
error:
    if (pty) {
        if (pty->in)
            ringbuf_free(pty->in);
        if (pty->out)
            ringbuf_free(pty->in);
        if (pty->tty)
            tty_free(pty->tty);

        kfree(pty);
    }

    return err;
}

int ptmx_open(struct file *file)
{   
    return pty_new(curproc, &(file->vnode));
}

int pty_init()
{
    kdev_chrdev_register(2, &ptmdev);

    for (devid_t i = 136; i < 144; ++i)
        kdev_chrdev_register(i, &ptsdev);

    return 0;
}

struct dev ptsdev = {
    .name  = "ptsdev",
    .read  = pts_read,
    .write = pts_write,
    .ioctl = pty_ioctl,

    .fops = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = pts_can_read,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    }
};

struct dev ptmdev = {
    .name = "ptmdev",
    .read  = ptm_read,
    .write = ptm_write,
    .ioctl = pty_ioctl,

    .fops = {
        .open  = ptmx_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};

MODULE_INIT(pty, pty_init, NULL)
