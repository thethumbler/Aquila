/** \file
 *          VFS => Generic file write function
 */

#include <core/system.h>

#include <fs/vfs.h>
#include <fs/iobuf.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>

/**
 * Up to io->resid bytes of the iobuf into `file', at its offset (moved
 * past them): how many went, or an error
 */
ssize_t posix_file_write(struct file *file, struct iobuf *io)
{
    if (!(file->flags & (O_WRONLY | O_RDWR)))   /* File is not opened for writing */
        return -EBADF;
    
    if (file->flags & O_NONBLOCK) { /* Non-blocking I/O */
        if (vfs_file_can_write(file, io->resid)) {
            /* write up to io->resid from the iobuf into file */
            ssize_t retval = vfs_write_io(file->vnode, file->offset, io);

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
        size_t size = io->resid;
        
        while (io->resid) {
            ssize_t r = vfs_write_io(file->vnode, file->offset + (size - io->resid), io);

            if (r < 0) {    /* Error */
                /* report the error only if nothing was written */
                if (io->resid == size)
                    return r;
                break;
            }

            /* No bytes left to be written, the buffer faulted, or reached
             * END-OF-FILE */
            if (!io->resid || io->fault || vfs_file_eof(file))    /* Done writting */
                break;

            /* readers may be asleep waiting for what was just written
             * (a pipe's ring holds less than one stdio buffer) */
            if (file->vnode->read_queue)
                thread_queue_wakeup(file->vnode->read_queue);

            /* nothing would wake a writer up, nothing more fits */
            if (!file->vnode->write_queue)
                break;

            /* Sleep on the file writers queue; a signal: what was
             * written, or the call ends for it */
            int err = thread_queue_sleep(file->vnode->write_queue);

            if (err) {
                if (io->resid == size)
                    return err;
                break;
            }
        }
        
        /* Store written bytes count */
        ssize_t retval = size - io->resid;

        /* Update file offset */
        file->offset += retval;

        /* Wake up all sleeping readers if a `read_queue' is attached */
        if (file->vnode->read_queue)
            thread_queue_wakeup(file->vnode->read_queue);

        return retval;
    }
}
