#include <core/system.h>
#include <core/module.h>

#include <fs/vfs.h>
#include <fs/tmpfs.h>
#include <fs/devpts.h>

/* devpts root directory (usually mounted on /dev/pts) */
struct inode *devpts_root = NULL;
struct vnode vdevpts_root;

int devpts_init()
{
    /* devpts is really just tmpfs */
    devpts.iops = tmpfs.iops;
    devpts.fops = tmpfs.fops;

    devpts_root = NULL;
    
    if (!(devpts_root = kmalloc(sizeof(struct inode))))
        return -ENOMEM;

    memset(devpts_root, 0, sizeof(struct inode));

    devpts_root->ino   = (vino_t) devpts_root;
    devpts_root->mode  = S_IFDIR;
    devpts_root->nlink = 2;
    devpts_root->fs    = &devpts;

    vdevpts_root.super = devpts_root;
    vdevpts_root.ino   = (vino_t) devpts_root;
    vdevpts_root.mode  = S_IFDIR;

    vfs_install(&devpts);

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
