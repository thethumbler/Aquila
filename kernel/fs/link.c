#include <core/system.h>
#include <fs/vfs.h>
#include <aquila/fcntl.h>

/* root vnode of the filesystem mounted where absolute path `path' is */
static struct vnode *mount_root(const char *path)
{
    char **tokens = tokenize_path(path);
    struct vfs_path *vfs_path = vfs_get_mountpoint(tokens);
    struct vnode *root = vfs_path->root;

    free_tokens(tokens);
    kfree(vfs_path);
    return root;
}

/* a new hard link `newpath' to `oldpath', which is followed unless uio has O_NOFOLLOW */
int vfs_link(const char *oldpath, const char *newpath, struct uio *uio)
{
    int ret = 0;
    struct vnode *vnode = NULL, *dir = NULL;
    char *oabs = NULL, *nabs = NULL, *name = NULL;

    if ((ret = vfs_lookup(oldpath, uio, &vnode, &oabs)))
        return ret;

    if (S_ISDIR(vnode->mode)) {
        ret = -EPERM;
        goto done;
    }

    if ((ret = vfs_lookup_parent(newpath, uio, &dir, &name, &nabs))) {
        if (ret == -EBUSY)
            ret = -EEXIST;
        goto done;
    }

    if (mount_root(oabs) != mount_root(nabs)) {
        ret = -EXDEV;
        goto done;
    }

    if ((ret = vfs_access(dir, uio, VFS_W | VFS_X)))
        goto done;

    ret = vfs_vlink(dir, name, vnode, uio);

done:
    if (dir) vfs_close(dir);
    vfs_close(vnode);
    kfree(oabs);
    kfree(nabs);
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

    ssize_t ret = vfs_lookup(path, &link_uio, &vnode, NULL);

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

int vfs_rename(const char *oldpath, const char *newpath, struct uio *uio)
{
    int ret = 0;
    struct vnode *odir = NULL, *ndir = NULL, *ovnode = NULL, *nvnode = NULL;
    char *oname = NULL, *nname = NULL, *oabs = NULL, *nabs = NULL;

    if ((ret = vfs_lookup_parent(oldpath, uio, &odir, &oname, &oabs)))
        goto done;

    if ((ret = vfs_lookup_parent(newpath, uio, &ndir, &nname, &nabs)))
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
    if (ovnode == nvnode || !strcmp(oabs, nabs))
        goto done;

    if (mount_root(oabs) != mount_root(nabs)) {
        ret = -EXDEV;
        goto done;
    }

    size_t olen = strlen(oabs);

    /* a directory can't be moved into itself */
    if (S_ISDIR(ovnode->mode) && !strncmp(oabs, nabs, olen) && nabs[olen] == '/') {
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
    kfree(oabs);
    kfree(nabs);
    return ret;
}
