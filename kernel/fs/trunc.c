#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

int vfs_trunc(struct inode *inode, off_t len)
{
    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return -EINVAL;

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.trunc)
        return -ENOSYS;

    return inode->fs->iops.trunc(inode, len);
}

