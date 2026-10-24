/** \file
 * Generic answers, for regular files, to whether a read is at the end of
 * the file. (Whether a read or a write of a regular file can go on is
 * always yes: `vfs_file_can_read()` and `vfs_file_can_write()` say so
 * themselves.)
 */

#include <fs/posix.h>

/**
 * Says whether a file's offset is at or past its end, so that a read
 * returns nothing.
 *
 * @param file  the open file
 * @return whether the offset is at the end
 */
int posix_file_eof(struct file *file)
{
    return (size_t) file->offset >= file->vnode->size;
}
