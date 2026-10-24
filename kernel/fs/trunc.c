/** \file
 * Truncating a vnode: changing a file's length.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>
#include <mm/vm.h>

/**
 * Changes a file's length, through its filesystem. The file's pages in
 * the page cache between the old end and the new one are refreshed.
 *
 * @param vnode  the file
 * @param len    the new length, in bytes
 * @return 0, `-EINVAL` for a device or an invalid vnode, `-ENOSYS` if
 *         the filesystem can't truncate, or its error
 */
int vfs_trunc(struct vnode *vnode, off_t len)
{
    vfs_log(LOG_DEBUG, "vfs_trunc(vnode=%p, len=%d)\n", vnode, len);

    /* invalid request */
    if (!vnode)
        return -EINVAL;

    /* a device node has no length to change */
    if (ISDEV(vnode))
        return -EINVAL;

    /* invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* the filesystem doesn't support it */
    if (!vnode->fs->vops.trunc)
        return -ENOSYS;

    off_t old = vnode->size;
    int err = vnode->fs->vops.trunc(vnode, len);

    /* refresh the file's pages in the page cache between the old end and
     * the new one */
    if (!err)
        vnode_pages_update(vnode, MIN(old, len), MAX(old, len));

    return err;
}

