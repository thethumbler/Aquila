#include <fs/posix.h>
#include <bits/fcntl.h>
#include <fs/stat.h>
#include <sys/sched.h>

int posix_file_open(struct file *file)
{
    /* Check permissions */

    if (cur_thread->owner->uid == 0) {    /* Root */
        return 0;
    }

    uint32_t mask = file->node->mask;
    uint32_t uid  = file->node->uid;
    uint32_t gid  = file->node->gid;

    if ((file->flags & O_ACCMODE) == O_RDONLY || (file->flags & O_ACCMODE) == O_RDWR) {
        if (uid == cur_thread->owner->uid) {
            if (mask & S_IRUSR)
                return 0;
        } else if (gid == cur_thread->owner->gid) {
            if (mask & S_IRGRP)
                return 0;
        } else {
            if (mask & S_IROTH)
                return 0;
        }

        return -EACCES;
    } else if (file->flags & (O_WRONLY | O_RDWR)) {
        /* TODO */
        return 0;
    }
    
    return -EPERM;   /* XXX */
}
