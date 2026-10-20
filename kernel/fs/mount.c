#include <core/system.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <dev/dev.h>
#include <ds/queue.h>

struct queue *mounts = QUEUE_NEW();

MALLOC_DEFINE(M_MOUNTPOINT, "mountpoint", "mount point structure");

int vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio)
{
    struct fs *fs = NULL;

    /* Look up filesystem */
    for (struct fs_list *entry = registered_fs; entry; entry = entry->next) {
        if (!strcmp(entry->name, type)) {
            fs = entry->fs;
            break;
        }
    }

    if (!fs)
        return -EINVAL;

    /* Directory path must be absolute */
    int ret = 0;
    char *_dir = NULL;
    if ((ret = vfs_parse_path(dir, uio, &_dir))) {
        if (_dir)
            kfree(_dir);
        return ret;
    }

    int err = 0;
    err = fs->mount(_dir, flags, data);
    
    if (err == 0) {
        struct mountpoint *mp = kmalloc(sizeof(struct mountpoint), &M_MOUNTPOINT, 0);
        struct {
            char *dev;
            char *opt;
        } *args = data;
        mp->dev = (args && args->dev)? strdup(args->dev) : "none";
        mp->type = strdup(type);
        mp->path = strdup(_dir);
        mp->options = "";
        enqueue(mounts, mp);
    }

    kfree(_dir);

    return err;
}

/* the block device of a disk partition, /dev/hda1 to /dev/hdd4 (ATA) */
static int root_dev(const char *name, dev_t *dev)
{
    if (strncmp(name, "/dev/hd", 7) || name[7] < 'a' || name[7] > 'd' ||
        name[8] < '1' || name[8] > '4' || name[9])
        return -ENODEV;

    *dev = DEV(3, (name[7] - 'a') * 64 + name[8] - '0');
    return 0;
}

/*
 * The root filesystem from a disk partition (root=/dev/hda2), of the type
 * given (rootfstype=, ext2 if NULL). Devices have no nodes yet, the
 * partition's vnode is made here as the ramdisk's is.
 */
int vfs_mount_root_dev(const char *name, const char *type)
{
    struct fs *fs = NULL;
    dev_t rdev;
    int err;

    if ((err = root_dev(name, &rdev)))
        return err;

    if (!type)
        type = "ext2";

    for (struct fs_list *entry = registered_fs; entry; entry = entry->next) {
        if (!strcmp(entry->name, type)) {
            fs = entry->fs;
            break;
        }
    }

    if (!fs || !fs->load)
        return -EINVAL;

    struct vnode *dev = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!dev) return -ENOMEM;

    dev->mode = S_IFBLK;
    dev->rdev = rdev;
    dev->fs   = &devfs;
    dev->ref  = 1;

    struct vnode *root = NULL;

    if ((err = fs->load(dev, &root))) {
        kfree(dev);
        return err;
    }

    printk("kernel: root is %s (%s)\n", name, type);
    return vfs_mount_root(root);
}
