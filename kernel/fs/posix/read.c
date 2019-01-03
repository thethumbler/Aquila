/*
 *          VFS => Generic file read function
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <bits/fcntl.h>
#include <bits/errno.h>
#include <bits/dirent.h>

/**
 * posix_file_read
 *
 * Reads up to `size' bytes from a file to `buf'.
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 * 
 * @file    File Descriptor for the function to operate on.
 * @buf     Buffer to write to.
 * @size    Number of bytes to read.
 * @returns read bytes on success, or error-code on failure.
 */

ssize_t posix_file_read(struct file *file, void *buf, size_t size)
{
    if (file->flags & O_WRONLY) /* File is not opened for reading */
        return -EBADFD;

    if (!size)
        return 0;
    
    int retval;
    for (;;) {
        if ((retval = vfs_read(file->inode, file->offset, size, buf)) > 0) {
            /* Update file offset */
            file->offset += retval;
            
            /* Wake up all sleeping writers if a `write_queue' is attached */
            if (file->inode->write_queue)
                thread_queue_wakeup(file->inode->write_queue);

            /* Return read bytes count */
            return retval;
        } else if (retval < 0) {    /* Error */
            return retval;
        } else if (vfs_file_eof(file)) {
            /* Reached end-of-file */
            return 0;
        } else if (file->flags & O_NONBLOCK) {
            /* Can not satisfy read operation, would block */
            return -EAGAIN;
        } else {
            /* Block until some data is available */
            /* Sleep on the file readers queue */
            if (thread_queue_sleep(file->inode->read_queue))
                return -EINTR;
        }
    }
}
