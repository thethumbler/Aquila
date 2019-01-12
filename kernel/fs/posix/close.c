#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>

int posix_file_close(struct file *file)
{
    return vfs_close(file->inode);
}
