/** \file
 * devpts: the filesystem of the pseudo-terminals' slave devices, usually
 * mounted on `/dev/pts`. Like devfs, it is a tmpfs with one root of its
 * own, where opening `/dev/ptmx` makes `/dev/pts/<n>`.
 */

#include <core/system.h>
#include <core/module.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <fs/devpts.h>

/* devpts's root directory, usually mounted on /dev/pts */
struct vnode *devpts_root = NULL;

/**
 * Sets devpts up at boot: tmpfs's operations, and its root directory.
 *
 * @return 0, or a negative errno
 */
int devpts_init()
{
    int err = 0;

    /* devpts is a tmpfs */
    devpts.vops = tmpfs.vops;
    devpts.fops = tmpfs.fops;

    devpts_root = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!devpts_root) return -ENOMEM;

    devpts_root->ino   = (vino_t) devpts_root;
    devpts_root->mode  = S_IFDIR | 0775;
    devpts_root->nlink = 2;
    devpts_root->fs    = &devpts;

    struct timespec ts;
    gettime(&ts);

    devpts_root->ctime = ts;
    devpts_root->atime = ts;
    devpts_root->mtime = ts;

    err = vfs_install(&devpts);

    if (err) {
        kfree(devpts_root);
        devpts_root = NULL;
        return err;
    }

    return 0;
}

/**
 * Mounts devpts on a directory: its one root directory is bound there.
 *
 * @param dir    the directory to mount it on
 * @param flags  not used
 * @param data   not used
 * @return 0, `-EINVAL` if devpts wasn't set up, or a negative errno
 */
int devpts_mount(struct vnode *dir, int flags, void *data)
{
    if (!devpts_root)
        return -EINVAL;

    return vfs_bind(dir, devpts_root);
}

struct fs devpts = {
    .name  = "devpts",
    .nodev = 1,
    .init  = devpts_init,
    .mount = devpts_mount,
};

MODULE_INIT(devpts, devpts_init, NULL)
