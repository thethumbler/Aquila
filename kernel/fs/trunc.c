#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>
#include <mm/vm.h>

int vfs_trunc(struct vnode *vnode, off_t len)
{
    vfs_log(LOG_DEBUG, "vfs_trunc(vnode=%p, len=%d)\n", vnode, len);

    /* Invalid request */
    if (!vnode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(vnode))
        return -EINVAL;

    /* Invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!vnode->fs->vops.trunc)
        return -ENOSYS;

    off_t old = vnode->size;
    int err = vnode->fs->vops.trunc(vnode, len);

    /* the file's pages in the page cache, between the old end and the
     * new one */
    if (!err)
        vnode_pages_update(vnode, MIN(old, len), MAX(old, len));

    return err;
}

