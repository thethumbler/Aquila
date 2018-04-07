#include <fs/posix.h>

int posix_file_close(struct file *file __unused)
{
    vfs_close(file->node);
    return 0;
}
