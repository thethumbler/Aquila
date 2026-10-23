#include <core/system.h>
#include <fs/vfs.h>
#include <fs/iobuf.h>
#include <dev/dev.h>

/**
 * write data to a vnode, through an iobuf
 */
ssize_t vfs_write_io(struct vnode *vnode, off_t off, struct iobuf *io)
{
    vfs_log(LOG_DEBUG, "vfs_write_io(vnode=%p, off=%d, size=%d)\n", vnode, off, io->resid);

    /* Invalid request */
    if (!vnode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(vnode))
        return kdev_write(&VNODE_DEV(vnode), off, io);

    /* Invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!vnode->fs->vops.write)
        return -ENOSYS;

    return vnode->fs->vops.write(vnode, off, io);
}

/**
 * write data to a vnode from a kernel buffer
 */
ssize_t vfs_write(struct vnode *vnode, off_t off, size_t size, void *buf)
{
    struct iobuf io;
    iobuf_init(&io, buf, size, 0);
    return vfs_write_io(vnode, off, &io);
}
