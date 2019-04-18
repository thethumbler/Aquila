#include <core/system.h>
#include <fs/vfs.h>
#include <ds/queue.h>

struct queue *mounts = QUEUE_NEW();

MALLOC_DEFINE(M_MOUNTPOINT, "mountpoint", "mount point structure");

int vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio)
{
    struct fs *fs = NULL;

    /* Look up filesystem */
    forlinked (entry, registered_fs, entry->next) {
        if (!strcmp(entry->name, type)) {
            fs = entry->fs;
            break;
        }
    }

    if (!fs)
        return -EINVAL;

    /* Directory path must be absolute */
    int ret = 0;
    char *_dir = NULL;
    if ((ret = vfs_parse_path(dir, uio, &_dir))) {
        if (_dir)
            kfree(_dir);
        return ret;
    }

    int err = 0;
    err = fs->mount(_dir, flags, data);
    
    if (err == 0) {
        struct mountpoint *mp = kmalloc(sizeof(struct mountpoint), &M_MOUNTPOINT, 0);
        struct {
            char *dev;
            char *opt;
        } *args = data;
        mp->dev = args->dev? strdup(args->dev) : "none";
        mp->type = strdup(type);
        mp->path = strdup(_dir);
        mp->options = "";
        enqueue(mounts, mp);
    }

    kfree(_dir);

    return err;
}
