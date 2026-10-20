#include <fs/posix.h>
#include <aquila/fcntl.h>
#include <fs/stat.h>
#include <sys/sched.h>

int posix_file_open(struct file *file)
{
    /* O_TRUNC is for regular files, devices and FIFOs ignore it */
    if ((file->flags & O_TRUNC) && S_ISREG(file->vnode->mode)) {
        return vfs_file_trunc(file, 0);
    }

    return 0;
}
