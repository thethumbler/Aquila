#include <core/system.h>
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

    *devpts_root = (struct inode) {
        .type = FS_DIR,
        .id   = (vino_t) devpts_root,
        .fs   = &devpts,
    };

    vdevpts_root = (struct vnode) {
        .super = devpts_root,
        .id    = (vino_t) devpts_root,
        .type  = FS_DIR,
    };

    vfs_install(&devpts);

    return 0;
}

int devpts_mount(const char *dir, int flags, void *data)
{
    printk("devpts_mount(dir=%s, flags=%x, data=%p)\n", dir, flags, data);

    if (!devpts_root)
        return -EINVAL;

    return vfs_bind(dir, devpts_root);
}

struct fs devpts = {
    .name  = "devpts",
    .init  = devpts_init,
    .mount = devpts_mount,
};

MODULE_INIT(devpts, devpts_init, NULL);
