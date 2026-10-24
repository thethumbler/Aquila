/** \file
 * The generic truncate operation for files that are vnodes.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>

/**
 * Truncates (or extends) an open file to a new length.
 *
 * @param file  the open file
 * @param len   the new length, in bytes
 * @return 0, or a negative errno
 */
int posix_file_trunc(struct file *file, off_t len)
{
    return vfs_trunc(file->vnode, len);
}
