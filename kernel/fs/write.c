#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

ssize_t vfs_write(struct inode *inode, off_t offset, size_t size, void *buf)
{
    //vfs_log(LOG_DEBUG, "vfs_write(inode=%p, offset=%d, size=%d, buf=%p)\n", inode, offset, size, buf);

    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_write(&INODE_DEV(inode), offset, size, buf);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.write)
        return -ENOSYS;

    return inode->fs->iops.write(inode, offset, size, buf);
}

