#include <core/system.h>
#include <fs/vfs.h>

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

    return fs->mount(_dir, flags, data);
}

