#include <fs/posix.h>
#include <aquila/fcntl.h>
#include <fs/stat.h>
#include <sys/sched.h>

int posix_file_open(struct file *file)
{
    if (file->flags & O_TRUNC) {
        return vfs_file_trunc(file, 0);
    }

    return 0;
}
