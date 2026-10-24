/** \file
 * Closing a vnode: dropping a reference to it.
 */

#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>

/**
 * Drops a reference to a vnode. When the last one goes, the filesystem's
 * close operation releases it. Closing a vnode that has no references
 * left is a bug, and panics.
 *
 * @param vnode  the vnode
 * @return 0, `-EINVAL` if `vnode` isn't a valid vnode, or what the
 *         filesystem's close operation returns
 */
int vfs_close(struct vnode *vnode)
{
    vfs_log(LOG_DEBUG, "vfs_close(vnode=%p)\n", vnode);

    /* invalid request */
    if (!vnode || !vnode->fs)
        return -EINVAL;

    if (!vnode->ref)
        panic("closing an already closed vnode");

    vnode->ref--;

    /* filesystems without a close operation keep their vnodes around */
    if (!vnode->ref && vnode->fs->vops.close)
        return vnode->fs->vops.close(vnode);

    return 0;
}
