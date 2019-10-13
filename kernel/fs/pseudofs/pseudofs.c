#include <core/system.h>
#include <core/time.h>

#include <fs/pseudofs.h>

MALLOC_DEFINE(M_PSEUDOFS_DENT, "pseudofs-dirent", "pseudofs directory entry");

int pseudofs_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int err = 0;
    struct vnode *vnode = NULL;
    struct pseudofs_dirent *dirent = NULL;

    struct dirent dirent_tmp;
    if (!pseudofs_finddir(dir, fn, &dirent_tmp)) {
        err = -EEXIST;
        goto error;
    }

    vnode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!vnode) goto error_nomem;
    
    vnode->ino   = (ino_t) vnode;
    vnode->mode  = mode;
    vnode->size  = 0;
    vnode->uid   = uio->uid;
    vnode->gid   = uio->gid;
    vnode->nlink = S_ISDIR(mode)? 2 : 1;
    vnode->rdev  = dev;

    struct timespec ts;
    gettime(&ts);

    vnode->ctime = ts;
    vnode->atime = ts;
    vnode->mtime = ts;

    vnode->fs = dir->fs;    /* Copy filesystem from directory */

    struct pseudofs_dirent *cur_dir = (struct pseudofs_dirent *) dir->p;

    dirent = kmalloc(sizeof(struct pseudofs_dirent), &M_PSEUDOFS_DENT, 0);
    if (!dirent) goto error_nomem;

    dirent->d_ino  = vnode;
    dirent->d_name = strdup(fn);

    if (!dirent->d_name) goto error_nomem;

    dirent->next  = cur_dir;
    dir->p        = dirent;

    if (ref)
        *ref = vnode;

    return 0;

error_nomem:
    err = -ENOMEM;
    goto error;

error:
    if (vnode)
        kfree(vnode);

    if (dirent) {
        if (dirent->d_name)
            kfree((void *) dirent->d_name);

        kfree(dirent);
    }

    return err;
}

int pseudofs_vunlink(struct vnode *vnode, const char *fn, struct uio *uio)
{
    int err = 0;
    struct pseudofs_dirent *dir;
    struct vnode *next;
    struct pseudofs_dirent *prev, *cur;

    if (!S_ISDIR(vnode->mode))
        return -ENOTDIR;

    dir   = (struct pseudofs_dirent *) vnode->p;
    next  = NULL;

    if (!dir)  /* Directory not initialized */
        return -ENOENT;

    prev = cur = NULL;
    for (struct pseudofs_dirent *dirent = dir; dirent; dirent = dirent->next) {
        if (!strcmp(dirent->d_name, fn)) {
            cur = dirent;
            goto found;
        }

        prev = dirent;
    }

    return -ENOENT;    /* File not found */

found:
    if (prev)
        prev->next = cur->next;
    else
        vnode->p   = cur->next;

    cur->d_ino->nlink = 0;   /* XXX */

    if (cur->d_ino->ref == 0) {
        cur->d_ino->ref = 1; /* vfs_close will decrement ref */
        vfs_close(cur->d_ino);
    }

    return 0;
}

ssize_t pseudofs_readdir(struct vnode *dir, off_t offset, struct dirent *dirent)
{
    if (offset == 0) {
        strcpy(dirent->d_name, ".");
        return 1;
    }

    if (offset == 1) {
        strcpy(dirent->d_name, "..");
        return 1;
    }

    struct pseudofs_dirent *_dirent = (struct pseudofs_dirent *) dir->p;

    if (!_dirent)
        return 0;

    int found = 0;

    offset -= 2;

    int i = 0;
    for (struct pseudofs_dirent *e = _dirent; e; e = e->next) {
        if (i == offset) {
            found = 1;
            dirent->d_ino = e->d_ino->ino;
            strcpy(dirent->d_name, e->d_name);
            break;
        }
        ++i;
    }

    return found;
}

int pseudofs_finddir(struct vnode *dir, const char *name, struct dirent *dirent)
{
    if (!dir || !name || !dirent)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct pseudofs_dirent *_dirent = (struct pseudofs_dirent *) dir->p;
    if (!_dirent) return -ENOENT;

    for (struct pseudofs_dirent *e = _dirent; e; e = e->next) {
        if (!strcmp(name, e->d_name)) {
            dirent->d_ino = e->d_ino->ino;
            strcpy(dirent->d_name, e->d_name);
            return 0;
        }
    }

    return -ENOENT;
}

int pseudofs_close(struct vnode *vnode)
{
    /* XXX */
    //kfree(inode->name);
    kfree(vnode);
    return 0;
}
