/** \file
 * devfs: the filesystem of device nodes, usually mounted on `/dev`.
 *
 * It is a tmpfs with one root of its own: every mount of devfs shows the
 * same directory, so the nodes made at boot (rootfs's runit stage 1, or
 * a test program) are there for whoever mounts it.
 */

#include <core/system.h>
#include <core/module.h>
#include <core/string.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/tmpfs.h>

#include <aquila/errno.h>

/* devfs's root directory, usually mounted on /dev */
struct vnode *devfs_root = NULL;

/** Sets devfs up at boot: tmpfs's operations, and its root directory. */
static int devfs_init()
{
    /* devfs is a tmpfs */
    devfs.vops = tmpfs.vops;
    devfs.fops = tmpfs.fops;

    devfs_root = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!devfs_root) return -ENOMEM;

    devfs_root->ino   = (vino_t) devfs_root;
    devfs_root->mode  = S_IFDIR | 0775;
    devfs_root->nlink = 2;
    devfs_root->fs    = &devfs;
    devfs_root->ref   = 1;

    struct timespec ts;
    gettime(&ts);

    devfs_root->ctime = ts;
    devfs_root->atime = ts;
    devfs_root->mtime = ts;

    vfs_install(&devfs);

    return 0;
}

/** Mounts devfs on a directory: its one root directory is bound there. */
static int devfs_mount(struct vnode *dir, int flags, void *data)
{
    if (!devfs_root)
        return -EINVAL;

    return vfs_bind(dir, devfs_root);
}

struct fs devfs = {
    .name   = "devfs",
    .nodev  = 1,
    .init   = devfs_init,
    .mount  = devfs_mount,
};

MODULE_INIT(devfs, devfs_init, NULL)
