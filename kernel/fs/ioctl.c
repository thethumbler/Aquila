#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

int vfs_ioctl(struct inode *inode, unsigned long request, void *argp)
{
    //vfs_log(LOG_DEBUG, "vfs_ioctl(inode=%p, request=%ld, argp=%p)\n", inode, request, argp);

    /* TODO Basic ioctl handling */
    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_ioctl(&INODE_DEV(inode), request, argp);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.ioctl)
        return -ENOSYS;

    return inode->fs->iops.ioctl(inode, request, argp);
}

