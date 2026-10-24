/** \file
 * The generic open operation for files that are vnodes.
 */

#include <fs/posix.h>
#include <aquila/fcntl.h>
#include <fs/stat.h>
#include <sys/sched.h>

/**
 * Finishes opening a file: with `O_TRUNC`, a regular file is truncated
 * to 0 bytes.
 *
 * @param file  the file being opened
 * @return 0, or a negative errno if the truncation failed
 */
int posix_file_open(struct file *file)
{
    /* O_TRUNC is for regular files; devices and FIFOs ignore it */
    if ((file->flags & O_TRUNC) && S_ISREG(file->vnode->mode)) {
        return vfs_file_trunc(file, 0);
    }

    return 0;
}
