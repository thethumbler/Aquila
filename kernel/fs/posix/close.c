/** \file
 * The generic close operation for files that are vnodes.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>

/**
 * Closes an open file by dropping its reference to its vnode.
 *
 * @param file  the open file
 * @return 0, or a negative errno
 */
int posix_file_close(struct file *file)
{
    return vfs_close(file->vnode);
}
