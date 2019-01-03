#include <fs/posix.h>

int posix_file_can_read(struct file *file, size_t size)
{
    return (size_t) file->offset + size < file->inode->size;
}

int posix_file_can_write(struct file *file, size_t size)
{
    return (size_t) file->offset + size < file->inode->size;
}

int posix_file_eof(struct file *file)
{
    return (size_t) file->offset >= file->inode->size;
}
