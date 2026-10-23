#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>

/** close a vnode
 * closes a vnode (i.e. decrements reference counts)
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
