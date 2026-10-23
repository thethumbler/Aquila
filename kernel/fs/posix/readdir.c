/** \file
 *          VFS => Generic file readdir function
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>
#include <aquila/dirent.h>

/**
 * The entry of `file' (a directory) at its offset into `dirent', the
 * offset moved past it: 1, 0 at the end, or an error
 */
ssize_t posix_file_readdir(struct file *file, struct dirent *dirent)
{
    if (file->flags & O_WRONLY) /* File is not opened for reading */
        return -EBADF;
    
    int retval = vfs_readdir(file->vnode, file->offset, dirent);

    if (retval < 0) /* Error */
        return retval;

    /* Update file offset */
    file->offset += retval;
        
    /* Return read bytes count */
    return retval;
}
