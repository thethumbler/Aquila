#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>

/** close a vnode
 * \ingroup vfs
 * \brief closes a vnode (i.e. decrements reference counts)
 */
int vfs_close(struct vnode *vnode)
{
    vfs_log(LOG_DEBUG, "vfs_close(vnode=%p)\n", vnode);

    /* invalid request */
    if (!vnode || !vnode->fs)
        return -EINVAL;

    /* operation not supported */
    if (!vnode->fs->vops.close)
        return -ENOSYS;

    if (!vnode->ref)
        panic("closing an already closed vnode");

    vnode->ref--;

    if (!vnode->ref) {
        return vnode->fs->vops.close(vnode);
    }

    return 0;
}
