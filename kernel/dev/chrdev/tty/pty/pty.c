#include <core/system.h>
#include <fs/vfs.h>
#include <fs/devpts.h>
#include <fs/posix.h>
#include <dev/dev.h>
#include <dev/tty.h>
#include <sys/sched.h>
#include <bits/errno.h>

struct pty {
    size_t id;
    struct tty *tty;

    struct inode *master;
    struct inode *slave;

    queue_t pts_read_queue;  /* Slave read, Master write wait queue */
    queue_t pts_write_queue; /* Slave write, Master read wait queue */
};

static struct pty *__pty[256] = {0};

struct dev ptmdev;
struct dev ptsdev;

/*************************************
 *
 * Pseudo Terminal Master Helpers
 *
 *************************************/

static int ptm_new(struct pty *pty, struct inode **ref)
{
    /* Anonymous file, not populated */
    struct inode *ptm = NULL;
    
    if (!(ptm = kmalloc(sizeof(struct inode))))
        return -ENOMEM;

    memset(ptm, 0, sizeof(struct inode));

    ptm->rdev        = _DEV_T(2, pty->id);
    ptm->type        = FS_CHRDEV;
    ptm->read_queue  = &pty->pts_read_queue;
    ptm->write_queue = &pty->pts_write_queue;
    ptm->p           = pty;

    if (ref)
        *ref = ptm;

    return 0;
}

static ssize_t ptm_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = __pty[dd->minor];
    return tty_master_read(pty->tty, size, buf);  
}

static ssize_t ptm_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = __pty[dd->minor];
    return tty_master_write(pty->tty, size, buf);  
}

static int pty_ioctl(struct devid *dd, int request, void *argp)
{
    struct pty *pty = __pty[dd->minor];

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

static int pts_new(struct pty *pty, struct inode **ref)
{
    char name[12];
    memset(name, 0, 12);
    snprintf(name, 11, "%d", pty->id);

    dev_t dev = _DEV_T(136 + pty->id / 256, pty->id % 256);

    struct uio uio = {0};

    struct inode *pts = NULL;
    int err = 0;
    if ((err = vfs_vmknod(&vdevpts_root, name, FS_CHRDEV, dev, &uio, &pts)))
        return err;

    pts->read_queue = &pty->pts_read_queue;
    pts->write_queue = &pty->pts_write_queue;

    if (ref)
        *ref = pts;

    return 0;
}

ssize_t pts_read(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = __pty[dd->minor];
    return tty_slave_read(pty->tty, size, buf);
}

ssize_t pts_write(struct devid *dd, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = __pty[dd->minor];
    return tty_slave_write(pty->tty, size, buf);
}

/*************************************
 *
 * Pseudo Terminal Helpers
 *
 *************************************/

static inline size_t pty_id_get()
{
    /* XXX */
    static size_t id = 0;
    return id++;
}

int pty_new(proc_t *proc, struct inode **master)
{
    int err = 0;
    struct pty *pty = NULL;

    if (!(pty = kmalloc(sizeof(struct pty)))) {
        err = -ENOMEM;
        goto error;
    }

    memset(pty, 0, sizeof(struct pty));

    pty->id = pty_id_get();

    if ((err = tty_new(proc, 0, &pty->tty)))
        goto error;

    if ((err = ptm_new(pty, &pty->master)))
        goto error;

    if (master)
        *master = pty->master;
    
    if ((err = pts_new(pty, &pty->slave)))
        goto error;

    __pty[pty->id] = pty;

    printk("[%d] %s: Created ptm/pts pair id=%d\n", proc->pid, proc->name, pty->id);
    return 0;

error:
    if (pty) {
        if (pty->tty)
            tty_free(pty->tty);

        kfree(pty);
    }

    return err;
}

int ptmx_open(struct file *file)
{   
    return pty_new(cur_thread->owner, &(file->node));
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

        .can_read  = __always,
        .can_write = __always,
        .eof       = __never,
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

        .can_read  = __always,
        .can_write = __always,
        .eof       = __never,
    },
};

MODULE_INIT(pty, pty_init, NULL);
