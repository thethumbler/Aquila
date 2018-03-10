#include <core/system.h>

#include <fs/vfs.h>
#include <bits/errno.h>

/**
 * posix_file_lseek
 *
 * Conforming to `IEEE Std 1003.1, 2013 Edition'
 *
 */

ssize_t posix_file_lseek(struct file *file, off_t offset, int whence)
{
    struct inode *inode = file->node;

    switch (whence) {
        case 0: /* SEEK_SET */
            file->offset = offset;
            break;
        case 1: /* SEEK_CUR */
            file->offset += offset;
            break;
        case 2: /* SEEK_END */
            file->offset = inode->size + offset;
            break;
    }

    return file->offset;
}
