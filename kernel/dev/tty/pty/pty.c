#include <core/system.h>
#include <sys/uaccess.h>
#include <core/module.h>
#include <fs/vfs.h>
#include <fs/devpts.h>
#include <fs/posix.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <sys/sched.h>
#include <aquila/errno.h>
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
#define PTY_MAX 256

static struct pty *ptys[PTY_MAX] = {0};

/** reserved pty ids (allocated, possibly not yet fully initialized) */
static char pty_ids[PTY_MAX] = {0};

struct dev ptmdev;
struct dev ptsdev;

/** lookup the pty associated with a master (major 2) or slave (major 136) device */
static struct pty *pty_get(struct devid *dd)
{
    if (dd->major != 2 && dd->major != 136)
        return NULL;

    /* minor is devid_t (uint8_t), always < PTY_MAX */
    return ptys[dd->minor];
}

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
    ptm->ref         = 1;   /* the descriptor ptmx was opened with */
    ptm->read_queue  = &pty->pts_read_queue;
    ptm->write_queue = &pty->pts_write_queue;
    ptm->p           = pty;

    if (ref)
        *ref = ptm;

    return 0;
}

static ssize_t ptm_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = pty_get(dd);
    if (!pty) return -ENXIO;

    size_t ret = ringbuf_read(pty->out, size, buf);  

    thread_queue_wakeup(&pty->pts_read_queue);

    return ret;
}

static ssize_t ptm_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = pty_get(dd);
    if (!pty) return -ENXIO;

    return tty_master_write(pty->tty, size, buf);  
}

static int pty_ioctl(struct devid *dd, int request, void *argp)
{
    struct pty *pty = pty_get(dd);
    if (!pty) return -ENXIO;

    switch (request) {
        case TIOCGPTN:
            if (uaccess_check(argp, sizeof(int)))
                return -EFAULT;
            *(int *) argp = pty->id;
            return 0;
        case TIOCSPTLCK:
            if (uaccess_check(argp, sizeof(int)))
                return -EFAULT;
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

    /* the opener's, writable by the group like other terminals */
    struct uio uio = {.uid = curproc->uid, .gid = curproc->gid};

    struct vnode *pts = NULL;
    int err = 0;
    if ((err = vfs_vmknod(devpts_root, name, S_IFCHR | 0620, dev, &uio, &pts)))
        return err;

    pts->read_queue = &pty->pts_read_queue;
    pts->write_queue = &pty->pts_write_queue;

    if (ref)
        *ref = pts;

    return 0;
}

ssize_t pts_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = pty_get(dd);
    if (!pty) return -ENXIO;

    int err = tty_background(pty->tty, SIGTTIN);
    if (err) return err;

    return ringbuf_read(pty->in, size, buf);
}

ssize_t pts_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = pty_get(dd);
    if (!pty) return -ENXIO;

    if (pty->tty->tios.c_lflag & TOSTOP)
        tty_background(pty->tty, SIGTTOU);

    return tty_slave_write(pty->tty, size, buf);
}

/* a read found no data */
static int pts_eof(struct file *file)
{
    struct pty *pty = pty_get(&VNODE_DEV(file->vnode));
    return !pty || tty_eof(pty->tty);
}

int pts_can_read(struct file *file, size_t size)
{
    struct devid *dd = &VNODE_DEV(file->vnode);
    struct pty *pty = pty_get(dd);
    if (!pty) return 1; /* let read report the error */

    return ringbuf_available(pty->in);
}

/*************************************
 *
 * Pseudo Terminal Helpers
 *
 *************************************/

/** reserve the first free pty id, returns -ENOSPC if all are in use */
static inline int pty_id_get(void)
{
    for (int id = 0; id < PTY_MAX; ++id) {
        if (!pty_ids[id]) {
            pty_ids[id] = 1;
            return id;
        }
    }

    return -ENOSPC;
}

int pty_new(struct proc *proc, struct vnode **master)
{
    int err = 0, id = -1;
    struct pty *pty = NULL;

    if ((id = pty_id_get()) < 0)
        return id;

    pty = kmalloc(sizeof(struct pty), &M_PTY, M_ZERO);
    if (!pty) goto e_nomem;

    pty->id = id;

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

    if ((err = pts_new(pty, &pty->slave)))
        goto error;

    pty->tty->vnode = pty->slave;
    ptys[pty->id] = pty;

    if (master)
        *master = pty->master;

    return 0;

e_nomem:
    err = -ENOMEM;
error:
    if (pty) {
        if (pty->in)
            ringbuf_free(pty->in);
        if (pty->out)
            ringbuf_free(pty->out);
        if (pty->tty)
            tty_free(pty->tty);
        if (pty->master)
            kfree(pty->master);

        kfree(pty);
    }

    pty_ids[id] = 0;

    return err;
}

/* the master was closed: the pty, its /dev/pts entry and its id go */
static void pty_free(struct pty *pty)
{
    char name[12] = {0};
    struct uio uio = {0};

    ptys[pty->id] = NULL;

    /* the session it controls is hung up */
    tty_free(pty->tty);

    /* slaves still open fail with ENXIO, wake up those waiting on them */
    thread_queue_wakeup(&pty->pts_read_queue);
    thread_queue_wakeup(&pty->pts_write_queue);
    pty->slave->read_queue  = NULL;
    pty->slave->write_queue = NULL;

    snprintf(name, 11, "%d", pty->id);
    vfs_vunlink(devpts_root, name, &uio);
    vfs_close(pty->slave);

    ringbuf_free(pty->in);
    ringbuf_free(pty->out);
    pty_ids[pty->id] = 0;
    kfree(pty);
}

int ptmx_open(struct file *file)
{
    struct vnode *ptmx = file->vnode;
    int err = pty_new(curproc, &file->vnode);

    /* the descriptor now refers to the new master */
    if (!err)
        vfs_close(ptmx);

    return err;
}

/* the master has no filesystem, its references are counted here */
static int ptm_close(struct file *file)
{
    struct vnode *ptm = file->vnode;

    if (--ptm->ref)
        return 0;

    pty_free(ptm->p);
    kfree(ptm);
    return 0;
}

static int pts_open(struct file *file)
{
    struct pty *pty = pty_get(&VNODE_DEV(file->vnode));

    if (!pty)
        return -ENXIO;

    tty_open(pty->tty, file);
    return posix_file_open(file);
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
        .open  = pts_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = pts_can_read,
        .can_write = __vfs_can_always,
        .eof       = pts_eof,
    }
};

struct dev ptmdev = {
    .name = "ptmdev",
    .read  = ptm_read,
    .write = ptm_write,
    .ioctl = pty_ioctl,

    .fops = {
        .open  = ptmx_open,
        .close = ptm_close,
        .read  = posix_file_read,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};

MODULE_INIT(pty, pty_init, NULL)
