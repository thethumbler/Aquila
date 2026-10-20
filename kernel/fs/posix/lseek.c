#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <bits/errno.h>

/**
 * posix_file_lseek
 *
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 *
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

    /* resulting offset would be negative */
    if (new_offset < 0)
        return -EINVAL;

    file->offset = new_offset;
    return file->offset;
}
