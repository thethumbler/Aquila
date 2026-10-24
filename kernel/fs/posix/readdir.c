/** \file
 * The generic readdir operation for directories that are vnodes.
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>
#include <aquila/dirent.h>

/**
 * Reads the directory entry at an open directory's offset, and moves the
 * offset past it. The offset counts entries, not bytes.
 *
 * @param file    the open directory
 * @param dirent  the entry, filled in
 * @return 1 if an entry was read, 0 at the end of the directory, or a
 *         negative errno
 */
ssize_t posix_file_readdir(struct file *file, struct dirent *dirent)
{
    if (file->flags & O_WRONLY) /* not open for reading */
        return -EBADF;
    
    int retval = vfs_readdir(file->vnode, file->offset, dirent);

    if (retval < 0)
        return retval;

    /* past the entries read */
    file->offset += retval;
        
    return retval;
}
