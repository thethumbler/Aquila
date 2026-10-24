/** \file
 * The generic ioctl operation for files that are vnodes.
 */

#include <core/system.h>

#include <fs/vfs.h>
#include <aquila/errno.h>

/**
 * Passes an `ioctl()` request on to the file's vnode (a device's, or its
 * filesystem's).
 *
 * @param file     the open file
 * @param request  the request
 * @param argp     its argument, often a pointer to user memory
 * @return the request's result, or a negative errno
 */
int posix_file_ioctl(struct file *file, int request, void *argp)
{
    return vfs_ioctl(file->vnode, request, argp);
}
