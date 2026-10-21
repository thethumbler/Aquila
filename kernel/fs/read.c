#include <core/system.h>
#include <fs/vfs.h>
#include <fs/iobuf.h>
#include <dev/dev.h>

/**
 * \ingroup vfs
 * \brief read data from a vnode, through an iobuf
 */
ssize_t vfs_read_io(struct vnode *vnode, off_t off, struct iobuf *io)
{
    vfs_log(LOG_DEBUG, "vfs_read_io(vnode=%p, off=%d, size=%d)\n", vnode, off, io->resid);

    /* invalid request */
    if (!vnode)
        return -EINVAL;

    /* device node */
    if (ISDEV(vnode))
        return kdev_read(&VNODE_DEV(vnode), off, io);

    /* invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* operation not supported */
    if (!vnode->fs->vops.read)
        return -ENOSYS;

    return vnode->fs->vops.read(vnode, off, io);
}

/**
 * \ingroup vfs
 * \brief read data from a vnode into a kernel buffer
 */
ssize_t vfs_read(struct vnode *vnode, off_t off, size_t size, void *buf)
{
    struct iobuf io;
    iobuf_init(&io, buf, size, 0);
    return vfs_read_io(vnode, off, &io);
}
