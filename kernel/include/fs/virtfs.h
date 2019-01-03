#ifndef _VIRTFS_H
#define _VIRTFS_H

#include <fs/vfs.h>

struct virtfs_dirent {
    const char        *d_name;
    struct inode      *d_ino;

    struct virtfs_dirent *next;
};

int virtfs_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref);
int virtfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int virtfs_vfind(struct vnode *dir, const char *fn, struct vnode *child);
ssize_t virtfs_readdir(struct inode *dir, off_t offset, struct dirent *dirent);
int virtfs_close(struct inode *inode);

#endif /* ! _VIRTFS_H */
