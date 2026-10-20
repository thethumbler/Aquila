/*
 *          VFS => Generic file write function
 */

#include <core/system.h>

#include <fs/vfs.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>

/**
 * posix_file_write
 *
 * Writes up to `size' bytes from `buf' into a file.
 *
 * @file    File Descriptor for the function to operate on.
 * @buf     Buffer to read from.
 * @size    Number of bytes to write.
 * @returns written bytes on success, or error-code on failure.
 */

ssize_t posix_file_write(struct file *file, void *buf, size_t size)
{
    if (!(file->flags & (O_WRONLY | O_RDWR)))   /* File is not opened for writing */
        return -EBADFD;
    
    if (file->flags & O_NONBLOCK) { /* Non-blocking I/O */
        if (vfs_file_can_write(file, size)) {
            /* write up to `size' from `buf' into file */
            ssize_t retval = vfs_write(file->vnode, file->offset, size, buf);

            if (retval < 0) /* Error */
                return retval;

            /* Update file offset */
            file->offset += retval;
            
            /* Wake up all sleeping readers if a `read_queue' is attached */
            if (file->vnode->read_queue)
                thread_queue_wakeup(file->vnode->read_queue);

            /* Return written bytes count */
            return retval;
        } else {
            /* Can not satisfy write operation, would block */
            return -EAGAIN;
        }
    } else {    /* Blocking I/O */
        ssize_t retval = size;
        char *_buf = buf;
        
        while (size) {
            ssize_t r = vfs_write(file->vnode, file->offset + (retval - size), size, _buf);

            if (r < 0) {    /* Error */
                /* report the error only if nothing was written */
                if ((size_t) retval == size)
                    return r;
                break;
            }

            size -= r;
            _buf += r;

            /* No bytes left to be written, or reached END-OF-FILE */
            if (!size || vfs_file_eof(file))    /* Done writting */
                break;

            /* Sleep on the file writers queue */
            thread_queue_sleep(file->vnode->write_queue);
        }
        
        /* Store written bytes count */
        retval -= size;

        /* Update file offset */
        file->offset += retval;

        /* Wake up all sleeping readers if a `read_queue' is attached */
        if (file->vnode->read_queue)
            thread_queue_wakeup(file->vnode->read_queue);

        return retval;
    }
}
