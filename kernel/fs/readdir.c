#include <core/system.h>
#include <fs/vfs.h>

ssize_t vfs_readdir(struct inode *inode, off_t offset, struct dirent *dirent)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.readdir)
        return -ENOSYS;

    return inode->fs->iops.readdir(inode, offset, dirent);
}

