#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>

int posix_file_trunc(struct file *file, off_t len)
{
    return vfs_trunc(file->vnode, len);
}
