/** \file
 * Reading a directory's entries, and finding one by name.
 */

#include <core/system.h>
#include <fs/vfs.h>

/**
 * Reads a directory's entry at an offset. Offsets count entries, starting
 * at 0.
 *
 * @param dir     the directory
 * @param off     which entry
 * @param dirent  the entry, filled in
 * @return 1 if an entry was read (the offset to add to get the next one),
 *         0 past the last entry, `-ENOTDIR` if `dir` isn't a directory,
 *         or another negative errno
 */
ssize_t vfs_readdir(struct vnode *dir, off_t off, struct dirent *dirent)
{
    vfs_log(LOG_DEBUG, "vfs_readdir(dir=%p, off=%d, dirent=%p)\n", dir, off, dirent);

    /* invalid request */
    if (!dir || !dir->fs)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    /* the filesystem doesn't support it */
    if (!dir->fs->vops.readdir)
        return -ENOSYS;

    return dir->fs->vops.readdir(dir, off, dirent);
}

/**
 * Finds the entry of a directory that has a name.
 *
 * @param dir     the directory
 * @param name    the name
 * @param dirent  the entry, filled in
 * @return 0, `-ENOENT` if there is no such entry, `-ENOTDIR` if `dir`
 *         isn't a directory, or another negative errno
 */
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

/**
 * Says whether a directory is empty, so that it can be removed. An empty
 * directory has no entries besides `.` and `..`.
 *
 * @param dir  the directory
 * @return 1 if it's empty, 0 if it isn't or it couldn't be read
 */
int vfs_dir_empty(struct vnode *dir)
{
    struct dirent dirent;
    off_t off = 0;
    ssize_t ret;

    while ((ret = vfs_readdir(dir, off, &dirent)) > 0) {
        if (strcmp(dirent.d_name, ".") && strcmp(dirent.d_name, ".."))
            return 0;

        off += ret;
    }

    return ret == 0;
}
