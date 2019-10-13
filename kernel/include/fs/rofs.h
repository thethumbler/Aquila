#ifndef _FS_ROFS_H
#define _FS_ROFS_H

ssize_t rofs_write(struct vnode *vnode, off_t offset, size_t size, void *buf);
int rofs_trunc(struct vnode *vnode, off_t len);
int rofs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int rofs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);

#endif /* _FS_ROFS_H */
