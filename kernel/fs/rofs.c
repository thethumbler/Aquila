#include <core/system.h>
#include <fs/vfs.h>
#include <fs/rofs.h>

ssize_t rofs_write(struct vnode *vnode, off_t offset, struct iobuf *io)
{
    return -EROFS;
}

int rofs_trunc(struct vnode *vnode, off_t len)
{
    return -EROFS;
}

int rofs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    return -EROFS;
}

int rofs_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    return -EROFS;
}

int rofs_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio)
{
    return -EROFS;
}

int rofs_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio)
{
    return -EROFS;
}

int rofs_setattr(struct vnode *vnode)
{
    return -EROFS;
}
