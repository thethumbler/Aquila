#ifndef _VIRTFS_H
#define _VIRTFS_H

#include <fs/vfs.h>

struct virtfs_dir {
    struct inode     *node;
    struct virtfs_dir *next;
};

int virtfs_vmknod(struct vnode *dir, const char *fn, itype_t type, dev_t dev, struct uio *uio, struct inode **ref);
int virtfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int virtfs_vfind(struct vnode *dir, const char *fn, struct vnode *child);
ssize_t virtfs_readdir(struct inode *dir, off_t offset, struct dirent *dirent);
int virtfs_close(struct inode *inode);

#endif /* !_VIRTFS_H */
