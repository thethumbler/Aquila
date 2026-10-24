/** \file
 * Making a filesystem node by its path: a file, a directory, a device
 * node, a FIFO or a symbolic link.
 */

#include <core/system.h>
#include <fs/vfs.h>

/**
 * Makes a node at a path. The caller's umask applies to the mode (but
 * not for a symbolic link), and it needs write and search permission on
 * the directory.
 *
 * @param path  the new node's path
 * @param mode  its type (`S_IF*`) and permissions
 * @param dev   its device number, for a device node
 * @param uio   who is asking (for permissions and the umask)
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, `-EEXIST` if something is there already, or a negative errno
 */
int vfs_mknod(const char *path, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int ret = 0;
    struct vnode *dir = NULL;
    char *name = NULL;

    if ((ret = vfs_lookup_parent(path, uio, &dir, &name)))
        return ret == -EBUSY ? -EEXIST : ret;

    /* creating an entry needs write and search permission on the directory */
    if ((ret = vfs_access(dir, uio, VFS_W | VFS_X)))
        goto done;

    /* symbolic links are always 0777 */
    if (!S_ISLNK(mode))
        mode &= ~(uio->mask & 0777);

    ret = vfs_vmknod(dir, name, mode, dev, uio, ref);

done:
    vfs_close(dir);
    kfree(name);
    return ret;
}

/**
 * Makes a directory at a path.
 *
 * @param path  the new directory's path
 * @param mode  its permissions
 * @param uio   who is asking
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, or a negative errno
 */
int vfs_mkdir(const char *path, mode_t mode, struct uio *uio, struct vnode **ref)
{
    return vfs_mknod(path, S_IFDIR | mode, 0, uio, ref);
}

/**
 * Makes a regular file at a path.
 *
 * @param path  the new file's path
 * @param mode  its permissions
 * @param uio   who is asking
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, or a negative errno
 */
int vfs_creat(const char *path, mode_t mode, struct uio *uio, struct vnode **ref)
{
    return vfs_mknod(path, S_IFREG | mode, 0, uio, ref);
}
