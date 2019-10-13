#include <core/system.h>
#include <fs/vfs.h>

/** read entries from a directory vnode
 * \ingroup vfs
 */
ssize_t vfs_readdir(struct vnode *dir, off_t off, struct dirent *dirent)
{
    vfs_log(LOG_DEBUG, "vfs_readdir(dir=%p, off=%d, dirent=%p)\n", dir, off, dirent);

    /* Invalid request */
    if (!dir || !dir->fs)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->fs->vops.readdir)
        return -ENOSYS;

    return dir->fs->vops.readdir(dir, off, dirent);
}

int vfs_finddir(struct vnode *dir, const char *name, struct dirent *dirent)
{
    vfs_log(LOG_DEBUG, "vfs_finddir(dir=%p, name=%s, dirent=%p)\n", dir, name, dirent);

    if (!dir || !dir->fs)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    if (!dir->fs->vops.finddir)
        return -ENOSYS;

    return dir->fs->vops.finddir(dir, name, dirent);
}
