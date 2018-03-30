#include <core/system.h>
#include <fs/virtfs.h>

int virtfs_vmknod(struct vnode *vdir, const char *fn, itype_t type, dev_t dev, struct uio *uio, struct inode **ref)
{
    printk("virtfs_vmknod(vdir=%p, fn=%s, type=%d, dev=%x, uio=%p, ref=%p)\n", vdir, fn, type, dev, uio, ref);
    int err = 0;
    struct inode *node = NULL;
    struct virtfs_dir *dirent = NULL;

    if (!(node = kmalloc(sizeof(struct inode)))) {
        err = -ENOMEM;
        goto error;
    }

    memset(node, 0, sizeof(struct inode));
    
    if (!(node->name = strdup(fn))) {
        err = -ENOMEM;
        goto error;
    }

    node->type = type;
    node->size = 0;
    node->uid  = uio->uid;
    node->gid  = uio->gid;
    node->mask = uio->mask;
    node->rdev = dev;

    struct inode *dir = NULL;

    if (vfs_vget(vdir, &dir)) {
        /* That's odd */
        err = -EINVAL;
        goto error;
    }

    node->fs  = dir->fs;    /* Copy filesystem from directory */

    struct virtfs_dir *_dir = (struct virtfs_dir *) dir->p;

    if (!(dirent = kmalloc(sizeof(struct virtfs_dir)))) {
        err = -ENOMEM;
        goto error;
    }

    dirent->next = _dir;
    dirent->node = node;

    dir->p = dirent;

    if (ref)
        *ref = node;

    return 0;

error:
    if (node) {
        if (node->name)
            kfree(node->name);
        kfree(node);
    }

    if (dirent)
        kfree(dirent);

    return err;
}

int virtfs_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    struct inode *inode = (struct inode *) dir->id;
    struct virtfs_dir *_dir = (struct virtfs_dir *) inode->p;
    struct inode *next = NULL;

    if (!_dir)  /* Directory not initialized */
        return -ENOENT;

    forlinked (file, _dir, file->next) {
        if (!strcmp(file->node->name, fn)) {
            next = file->node;
            goto found;
        }
    }

    return -ENOENT;    /* File not found */

found:
    if (child) {
        /* child->super should not be touched */
        child->id   = (vino_t) next;
        child->type = next->type;
        child->mask = next->mask;
        child->uid  = next->uid;
        child->gid  = next->gid;
    }

    return 0;
}

ssize_t virtfs_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    int i = 0;
    struct virtfs_dir *_dir = (struct virtfs_dir *) dir->p;

    if (!_dir)
        return 0;

    int found = 0;

    if (offset == 0) {
        strcpy(dirent->d_name, ".");
        return 1;
    }

    if (offset == 1) {
        strcpy(dirent->d_name, "..");
        return 1;
    }

    offset -= 2;

    forlinked (e, _dir, e->next) {
        if (i == offset) {
            found = 1;
            strcpy(dirent->d_name, e->node->name);   // FIXME
            break;
        }
        ++i;
    }

    return found;
}
