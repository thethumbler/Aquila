/** \file
 * Generic answers, for regular files, to whether a read or a write can
 * go on, and whether a read is at the end of the file.
 */

#include <fs/posix.h>

/**
 * Says whether `size` bytes can be read from a file's offset without
 * reaching its end.
 *
 * @param file  the open file
 * @param size  the number of bytes
 * @return whether there is that much left
 */
int posix_file_can_read(struct file *file, size_t size)
{
    return (size_t) file->offset + size < file->vnode->size;
}

/**
 * Says whether `size` bytes can be written at a file's offset without
 * going past its end.
 *
 * @param file  the open file
 * @param size  the number of bytes
 * @return whether there is room for them before the end
 */
int posix_file_can_write(struct file *file, size_t size)
{
    return (size_t) file->offset + size < file->vnode->size;
}

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
