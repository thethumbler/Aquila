#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

/**
 * \ingroup vfs
 * \brief read data from a vnode
 */
ssize_t vfs_read(struct vnode *vnode, off_t off, size_t size, void *buf)
{
    vfs_log(LOG_DEBUG, "vfs_read(vnode=%p, off=%d, size=%d, buf=%p)\n", vnode, off, size, buf);

    /* invalid request */
    if (!vnode)
        return -EINVAL;

    /* device node */
    if (ISDEV(vnode))
        return kdev_read(&VNODE_DEV(vnode), off, size, buf);

    /* invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* operation not supported */
    if (!vnode->fs->vops.read)
        return -ENOSYS;

    return vnode->fs->vops.read(vnode, off, size, buf);
}
