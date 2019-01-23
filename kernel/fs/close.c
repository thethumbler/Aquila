#include <core/system.h>
#include <fs/vfs.h>

int vfs_close(struct inode *inode)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.close)
        return -ENOSYS;

    --inode->ref;

    if (inode->ref <= 0) {   /* Why < ? */
        return inode->fs->iops.close(inode);
    }

    return 0;
}
