#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

/** read data from an inode
 * \ingroup vfs
 */
ssize_t vfs_read(struct inode *inode, off_t offset, size_t size, void *buf)
{
    //vfs_log(LOG_DEBUG, "vfs_read(inode=%p, offset=%d, size=%d, buf=%p)\n", inode, offset, size, buf);

    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_read(&INODE_DEV(inode), offset, size, buf);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.read)
        return -ENOSYS;

    return inode->fs->iops.read(inode, offset, size, buf);
}
