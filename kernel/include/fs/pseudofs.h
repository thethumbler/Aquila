#ifndef _FS_PSEUDOFS_H
#define _FS_PSEUDOFS_H

#include <fs/vfs.h>

struct pseudofs_dirent {
    const char        *d_name;
    struct vnode      *d_ino;

    struct pseudofs_dirent *next;
};

int pseudofs_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int pseudofs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);

ssize_t pseudofs_readdir(struct vnode *dir, off_t offset, struct dirent *dirent);
int pseudofs_finddir(struct vnode *dir, const char *name, struct dirent *dirent);

int pseudofs_close(struct vnode *vnode);

#endif /* ! _FS_PSEUDOFS_H */
