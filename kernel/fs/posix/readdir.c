/*
 *          VFS => Generic file readdir function
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>
#include <aquila/dirent.h>

/**
 * posix_file_readdir
 *
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 * 
 * @file    File Descriptor for the function to operate on.
 * @dirent  Buffer to write to.
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
