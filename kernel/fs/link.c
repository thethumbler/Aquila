#include <core/system.h>
#include <fs/vfs.h>
#include <aquila/fcntl.h>

/* a new hard link `newpath' (from nuio's directory) to `oldpath' (from
 * ouio's), which is followed unless ouio has O_NOFOLLOW */
int vfs_link(const char *oldpath, struct uio *ouio, const char *newpath, struct uio *nuio)
{
    int ret = 0;
    struct vnode *vnode = NULL, *dir = NULL, *odir = NULL;
    char *name = NULL;
    struct uio *uio = nuio;

    if ((ret = vfs_lookup_dir(oldpath, ouio, &vnode, &odir)))
        return ret;

    if (S_ISDIR(vnode->mode)) {
        ret = -EPERM;
        goto done;
    }

    if ((ret = vfs_lookup_parent(newpath, nuio, &dir, &name))) {
        if (ret == -EBUSY)
            ret = -EEXIST;
        goto done;
    }

    /* in the same mounted filesystem */
    if (vfs_mount_root_of(odir) != vfs_mount_root_of(dir)) {
        ret = -EXDEV;
        goto done;
    }

    if ((ret = vfs_access(dir, uio, VFS_W | VFS_X)))
        goto done;

    ret = vfs_vlink(dir, name, vnode, uio);

done:
    if (dir) vfs_close(dir);
    vfs_close(odir);
    vfs_close(vnode);
    kfree(name);
    return ret;
}

/* a symbolic link at `path' containing `target' */
int vfs_symlink(const char *target, const char *path, struct uio *uio)
{
    int ret = 0;
    struct vnode *vnode = NULL;
    size_t len = strlen(target);

    if (!len)
        return -ENOENT;

    if ((ret = vfs_mknod(path, S_IFLNK | 0777, 0, uio, &vnode)))
        return ret;

    ssize_t written = vfs_write(vnode, 0, len, (void *) target);
    vfs_close(vnode);

    if (written != (ssize_t) len) {
        vfs_unlink(path, uio);
        return written < 0 ? written : -EIO;
    }

    return 0;
}

/* the contents of symbolic link `path', not NUL terminated */
ssize_t vfs_readlink(const char *path, struct uio *uio, char *buf, size_t size)
{
    struct vnode *vnode = NULL;
    struct uio link_uio = *uio;
    link_uio.flags |= O_NOFOLLOW;

    ssize_t ret = vfs_lookup(path, &link_uio, &vnode);

    if (ret)
        return ret;

    if (S_ISLNK(vnode->mode))
        ret = vfs_read(vnode, 0, MIN(size, vnode->size), buf);
    else
        ret = -EINVAL;

    vfs_close(vnode);
    return ret;
}

/* look up the entry `name' of `dir', NULL if there is none */
static int entry_get(struct vnode *dir, const char *name, struct vnode **ref)
{
    struct dirent dirent;
    int ret = vfs_finddir(dir, name, &dirent);

    *ref = NULL;

    if (ret == -ENOENT)
        return 0;

    return ret ? ret : vfs_vget(dir, dirent.d_ino, ref);
}

/* `oldpath' (from ouio's directory) renamed `newpath' (from nuio's) */
int vfs_rename(const char *oldpath, struct uio *ouio, const char *newpath, struct uio *nuio)
{
    int ret = 0;
    struct vnode *odir = NULL, *ndir = NULL, *ovnode = NULL, *nvnode = NULL;
    char *oname = NULL, *nname = NULL;
    struct uio *uio = ouio;

    if ((ret = vfs_lookup_parent(oldpath, ouio, &odir, &oname)))
        goto done;

    if ((ret = vfs_lookup_parent(newpath, nuio, &ndir, &nname)))
        goto done;

    if ((ret = vfs_access(odir, uio, VFS_W | VFS_X)) ||
        (ret = vfs_access(ndir, uio, VFS_W | VFS_X)))
        goto done;

    if ((ret = entry_get(odir, oname, &ovnode)))
        goto done;

    if (!ovnode) {
        ret = -ENOENT;
        goto done;
    }

    if ((ret = entry_get(ndir, nname, &nvnode)))
        goto done;

    /* renaming a file to itself (or to another link to it) does nothing */
    if (ovnode == nvnode || (odir == ndir && !strcmp(oname, nname)))
        goto done;

    /* in the same mounted filesystem */
    if (vfs_mount_root_of(odir) != vfs_mount_root_of(ndir)) {
        ret = -EXDEV;
        goto done;
    }

    /* a directory can't be moved into itself */
    if (S_ISDIR(ovnode->mode) && vfs_under(ndir, ovnode)) {
        ret = -EINVAL;
        goto done;
    }

    if (nvnode) {
        if (S_ISDIR(ovnode->mode) && !S_ISDIR(nvnode->mode)) {
            ret = -ENOTDIR;
            goto done;
        }

        if (!S_ISDIR(ovnode->mode) && S_ISDIR(nvnode->mode)) {
            ret = -EISDIR;
            goto done;
        }

        if (S_ISDIR(nvnode->mode) && !vfs_dir_empty(nvnode)) {
            ret = -ENOTEMPTY;
            goto done;
        }
    }

    /* replaces an existing new entry */
    ret = vfs_vrename(odir, oname, ndir, nname, uio);

done:
    if (nvnode) vfs_close(nvnode);
    if (ovnode) vfs_close(ovnode);
    if (ndir) vfs_close(ndir);
    if (odir) vfs_close(odir);

    kfree(oname);
    kfree(nname);
    return ret;
}
