#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>

int vfs_close(struct inode *inode)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.close)
        return -ENOSYS;

    if (!inode->ref)
        panic("closing an already closed inode");

    inode->ref--;

    if (!inode->ref) {
        return inode->fs->iops.close(inode);
    }

    return 0;
}
