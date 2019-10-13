#include <core/system.h>
#include <core/module.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <fs/devpts.h>

/* devpts root directory (usually mounted on /dev/pts) */
struct vnode *devpts_root = NULL;

int devpts_init()
{
    int err = 0;

    /* devpts is really just tmpfs */
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

int devpts_mount(const char *dir, int flags, void *data)
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
