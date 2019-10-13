#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

ssize_t vfs_write(struct vnode *vnode, off_t off, size_t size, void *buf)
{
    vfs_log(LOG_DEBUG, "vfs_write(vnode=%p, off=%d, size=%d, buf=%p)\n", vnode, off, size, buf);

    /* Invalid request */
    if (!vnode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(vnode))
        return kdev_write(&VNODE_DEV(vnode), off, size, buf);

    /* Invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!vnode->fs->vops.write)
        return -ENOSYS;

    return vnode->fs->vops.write(vnode, off, size, buf);
}

