/** \file
 * The generic lseek operation for files that are vnodes, as POSIX
 * (IEEE Std 1003.1) describes it.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <aquila/errno.h>

/**
 * Moves an open file's offset: to `offset` (`SEEK_SET`), by `offset`
 * from where it is (`SEEK_CUR`), or by `offset` from the end of the file
 * (`SEEK_END`). The offset may go past the end of the file.
 *
 * @param file    the open file
 * @param offset  the offset, or the distance to move it
 * @param whence  `SEEK_SET`, `SEEK_CUR` or `SEEK_END`
 * @return the new offset, or `-EINVAL` if `whence` is wrong or the new
 *         offset would be negative
 */
off_t posix_file_lseek(struct file *file, off_t offset, int whence)
{
    struct vnode *vnode = file->vnode;
    off_t new_offset;

    switch (whence) {
        case 0: /* SEEK_SET */
            new_offset = offset;
            break;
        case 1: /* SEEK_CUR */
            new_offset = file->offset + offset;
            break;
        case 2: /* SEEK_END */
            new_offset = (off_t) vnode->size + offset;
            break;
        default:
            return -EINVAL;
    }

    /* the new offset would be negative */
    if (new_offset < 0)
        return -EINVAL;

    file->offset = new_offset;
    return file->offset;
}
