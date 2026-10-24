/** \file
 * The operations of a read-only filesystem that would change it: each
 * fails with `EROFS`. A read-only filesystem (initramfs) uses them in its
 * table of vnode operations.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <fs/rofs.h>

/**
 * Refuses a write to a read-only filesystem.
 *
 * @param vnode   the file
 * @param offset  where the write would be
 * @param io      the data
 * @return `-EROFS`
 */
ssize_t rofs_write(struct vnode *vnode, off_t offset, struct iobuf *io)
{
    return -EROFS;
}

/**
 * Refuses to truncate a file on a read-only filesystem.
 *
 * @param vnode  the file
 * @param len    the length asked for
 * @return `-EROFS`
 */
int rofs_trunc(struct vnode *vnode, off_t len)
{
    return -EROFS;
}

/**
 * Refuses to make a node on a read-only filesystem.
 *
 * @param dir   the directory
 * @param fn    the new node's name
 * @param mode  its type and permissions
 * @param dev   its device number
 * @param uio   who is asking
 * @param ref   not set
 * @return `-EROFS`
 */
int rofs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    return -EROFS;
}

/**
 * Refuses to remove a name from a read-only filesystem.
 *
 * @param dir  the directory
 * @param fn   the name
 * @param uio  who is asking
 * @return `-EROFS`
 */
int rofs_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    return -EROFS;
}

/**
 * Refuses to make a hard link on a read-only filesystem.
 *
 * @param dir    the directory
 * @param fn     the new name
 * @param vnode  the file to link
 * @param uio    who is asking
 * @return `-EROFS`
 */
int rofs_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio)
{
    return -EROFS;
}

/**
 * Refuses to rename on a read-only filesystem.
 *
 * @param odir  the old directory
 * @param ofn   the old name
 * @param ndir  the new directory
 * @param nfn   the new name
 * @param uio   who is asking
 * @return `-EROFS`
 */
int rofs_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio)
{
    return -EROFS;
}

/**
 * Refuses to change a node's attributes on a read-only filesystem.
 *
 * @param vnode  the node
 * @return `-EROFS`
 */
int rofs_setattr(struct vnode *vnode)
{
    return -EROFS;
}
