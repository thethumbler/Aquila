/** \file
 * The generic read operation for files that are vnodes: regular files,
 * and devices, pipes and FIFOs, which may have to wait for data.
 */

#include <core/system.h>

#include <fs/vfs.h>
#include <fs/iobuf.h>
#include <core/arch.h>
#include <dev/dev.h>
#include <sys/sched.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>
#include <aquila/dirent.h>

/**
 * Reads from an open file at its offset, and moves the offset past what
 * was read. If there is nothing to read yet, it waits for data (a pipe's,
 * a terminal's), unless the file is non-blocking.
 *
 * @param file  the open file
 * @param io    where the data goes, and how much is wanted
 * @return the number of bytes read, 0 at the end of the file, `-EAGAIN`
 *         if a non-blocking read would wait, `-ERESTARTSYS` if a signal
 *         came while it waited, or another negative errno
 */
ssize_t posix_file_read(struct file *file, struct iobuf *io)
{
    if (file->flags & O_WRONLY) /* not open for reading */
        return -EBADF;

    if (!io->resid)
        return 0;
    
    /* a device may limit the wait (a terminal's VTIME) */
    uint64_t until = 0;
    struct fops *fops = ISDEV(file->vnode) ? kdev_fops(&VNODE_DEV(file->vnode)) :
                        file->vnode->fs ? &file->vnode->fs->fops : NULL;

    if (fops && fops->read_timeout) {
        uint64_t timeout = fops->read_timeout(file);

        if (timeout)
            until = arch_rtime_ns() + timeout;
    }

    int retval;
    for (;;) {
        if ((retval = vfs_read_io(file->vnode, file->offset, io)) > 0) {
            file->offset += retval;
            
            /* there's room now: wake up the writers waiting for some */
            if (file->vnode->write_queue)
                waitq_wakeup(file->vnode->write_queue);

            return retval;
        } else if (retval < 0) {
            return retval;
        } else if (vfs_file_eof(file)) {
            return 0;
        } else if (file->flags & O_NONBLOCK) {
            /* the read would have to wait */
            return -EAGAIN;
        } else if (!file->vnode->read_queue) {
            /* nothing would wake a reader up, there is no more to read */
            return 0;
        } else {
            /* wait until there is data, the time is up (0 bytes), or a
             * signal comes */
            int r = sched_wait(file->vnode->read_queue, until);

            if (r)
                return r < 0 ? r : 0;
        }
    }
}
