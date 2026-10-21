#include <core/system.h>
#include <fs/vfs.h>

/* remove the entry at path, a directory if `rmdir' is set */
static int vfs_remove(const char *path, struct uio *uio, int rmdir)
{
    int ret = 0;
    struct vnode *dir = NULL, *vnode = NULL;
    char *name = NULL;
    struct dirent dirent;

    if ((ret = vfs_lookup_parent(path, uio, &dir, &name)))
        return ret;

    /* removing an entry needs write and search permission on the directory */
    if ((ret = vfs_access(dir, uio, VFS_W | VFS_X)))
        goto done;

    if ((ret = vfs_finddir(dir, name, &dirent)))
        goto done;

    if ((ret = vfs_vget(dir, dirent.d_ino, &vnode)))
        goto done;

    if (rmdir) {
        if (!S_ISDIR(vnode->mode))
            ret = -ENOTDIR;
        else if (!vfs_dir_empty(vnode))
            ret = -ENOTEMPTY;
    } else if (S_ISDIR(vnode->mode)) {
        ret = -EISDIR;
    }

    if (!ret)
        ret = vfs_vunlink(dir, name, uio);

done:
    /* the vnode is freed here if that was its last link */
    if (vnode)
        vfs_close(vnode);

    vfs_close(dir);
    kfree(name);
    return ret;
}

int vfs_unlink(const char *path, struct uio *uio)
{
    return vfs_remove(path, uio, 0);
}

int vfs_rmdir(const char *path, struct uio *uio)
{
    return vfs_remove(path, uio, 1);
}
