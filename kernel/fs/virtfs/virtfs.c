#include <core/system.h>
#include <fs/virtfs.h>

int virtfs_vmknod(struct vnode *vdir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref)
{
    int err = 0;
    struct inode *inode = NULL;
    struct virtfs_dirent *dirent = NULL;
    struct inode *dir = NULL;

    /* Make sure file does not exist */
    if (!virtfs_vfind(vdir, fn, NULL)) {
        err = -EEXIST;
        goto error;
    }

    inode = kmalloc(sizeof(struct inode));
    if (!inode) goto error_nomem;

    memset(inode, 0, sizeof(struct inode));
    
    inode->mode  = mode;
    inode->size  = 0;
    inode->uid   = uio->uid;
    inode->gid   = uio->gid;
    inode->nlink = S_ISDIR(mode)? 2 : 1;
    inode->rdev  = dev;

    if (vfs_vget(vdir, &dir)) {
        /* That's odd */
        err = -EINVAL;
        goto error;
    }

    inode->fs = dir->fs;    /* Copy filesystem from directory */

    struct virtfs_dirent *cur_dir = (struct virtfs_dirent *) dir->p;

    dirent = kmalloc(sizeof(struct virtfs_dirent));
    if (!dirent) goto error_nomem;

    dirent->d_ino  = inode;
    dirent->d_name = strdup(fn);

    if (!dirent->d_name) goto error_nomem;

    dirent->next  = cur_dir;
    dir->p        = dirent;

    if (ref)
        *ref = inode;

    vfs_close(dir);

    return 0;

error_nomem:
    err = -ENOMEM;
    goto error;

error:
    if (inode)
        kfree(inode);

    if (dirent) {
        if (dirent->d_name)
            kfree((void *) dirent->d_name);

        kfree(dirent);
    }

    return err;
}

int virtfs_vunlink(struct vnode *vdir, const char *fn, struct uio *uio)
{
    int err = 0;
    struct inode *inode;
    struct virtfs_dirent *dir;
    struct inode *next;
    struct virtfs_dirent *prev, *cur;

    if (!S_ISDIR(vdir->mode))
        return -ENOTDIR;

    inode = (struct inode *) vdir->ino;
    dir   = (struct virtfs_dirent *) inode->p;
    next  = NULL;

    if (!dir)  /* Directory not initialized */
        return -ENOENT;


    prev = cur = NULL;
    forlinked (dirent, dir, dirent->next) {
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
        inode->p   = cur->next;

    cur->d_ino->nlink = 0;   /* XXX */

    if (cur->d_ino->ref == 0) {
        cur->d_ino->ref = 1; /* vfs_close will decrement ref */
        vfs_close(cur->d_ino);
    }

    return 0;
}

int virtfs_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct inode *inode = (struct inode *) dir->ino;
    struct virtfs_dirent *_dir = (struct virtfs_dirent *) inode->p;
    struct inode *next = NULL;

    if (!_dir)  /* Directory not initialized */
        return -ENOENT;

    forlinked (dirent, _dir, dirent->next) {
        if (!strcmp(dirent->d_name, fn)) {
            next = dirent->d_ino;
            goto found;
        }
    }

    return -ENOENT;    /* File not found */

found:
    if (child) {
        /* child->super should not be touched */
        child->ino  = (vino_t) next;
        child->mode = next->mode;
        child->uid  = next->uid;
        child->gid  = next->gid;
    }

    return 0;
}

ssize_t virtfs_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    if (offset == 0) {
        strcpy(dirent->d_name, ".");
        return 1;
    }

    if (offset == 1) {
        strcpy(dirent->d_name, "..");
        return 1;
    }

    struct virtfs_dirent *_dirent = (struct virtfs_dirent *) dir->p;

    if (!_dirent)
        return 0;

    int found = 0;

    offset -= 2;

    int i = 0;
    forlinked (e, _dirent, e->next) {
        if (i == offset) {
            found = 1;
            strcpy(dirent->d_name, e->d_name);
            break;
        }
        ++i;
    }

    return found;
}

int virtfs_close(struct inode *inode)
{
    /* XXX */
    //kfree(inode->name);
    kfree(inode);
    return 0;
}
