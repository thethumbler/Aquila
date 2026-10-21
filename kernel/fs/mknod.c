#include <core/system.h>
#include <fs/vfs.h>

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

int vfs_mkdir(const char *path, mode_t mode, struct uio *uio, struct vnode **ref)
{
    return vfs_mknod(path, S_IFDIR | mode, 0, uio, ref);
}

int vfs_creat(const char *path, mode_t mode, struct uio *uio, struct vnode **ref)
{
    return vfs_mknod(path, S_IFREG | mode, 0, uio, ref);
}
