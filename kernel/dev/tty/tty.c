/** \file
 * tty device
 */

#include <core/system.h>
#include <core/module.h>
#include <dev/ttydev.h>
#include <fs/posix.h>
#include <sys/sched.h>
#include <dev/tty.h>
#include <dev/vt.h>
#include <fs/vfs.h>

/** /dev/tty1 to /dev/tty6 are virtual terminals (/dev/tty0 the foreground
 * one), /dev/ttyS0 and up uarts */
struct dev *sttydev_mux(struct devid *dd)
{
    if (dd->minor >= 64)
        return &uart;

    if (dd->minor <= VT_COUNT)
        return &vtdev;

    return NULL;
}

/** /dev/tty: opens the controlling terminal's device instead */
static int ctty_open(struct file *file)
{
    struct tty *tty = curproc->pgrp->session->ctty;
    struct vnode *vnode = file->vnode;

    if (!tty || !tty->vnode)
        return -ENXIO;

    tty->vnode->ref++;
    file->vnode = tty->vnode;

    int err = kdev_file_open(&VNODE_DEV(tty->vnode), file);

    if (err) {
        vfs_close(tty->vnode);
        file->vnode = vnode;
        return err;
    }

    vfs_close(vnode);
    return 0;
}

static struct dev cttydev = {
    .name = "ctty",
    .fops = {
        .open = ctty_open,
    },
};

struct dev *ttydev_mux(struct devid *dd)
{
    switch (dd->minor) {
        case 0: /* /dev/tty */
            return &cttydev;
        case 1: /* /dev/console, the first virtual terminal */
            return &vtdev;
        case 2: /* /dev/ptmx */
            return &ptmdev;
    }

    return NULL;
}

int ttydev_probe()
{
    kdev_chrdev_register(4, &sttydev);
    kdev_chrdev_register(5, &ttydev);
    return 0;
}

struct dev sttydev = {
    .name  = "sttydev",
    .mux   = sttydev_mux,
};

struct dev ttydev = {
    .name  = "ttydev",
    .mux   = ttydev_mux,
};

MODULE_INIT(ttydev, ttydev_probe, NULL)
