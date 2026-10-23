/** \file
 * virtual filesystem
 */

#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <aquila/fcntl.h>
#include <aquila/errno.h>
#include <dev/dev.h>
#include <net/socket.h>

MALLOC_DEFINE(M_VNODE, "vnode", "vnode structure");
MALLOC_DEFINE(M_FS_LIST, "fs-list", "filesystems list");

static int vfs_log_level = LOG_NONE;
LOGGER_DEFINE(vfs, vfs_log, vfs_log_level)

/** list of registered filesystems */
struct fs_list *registered_fs = NULL;

/* ================== Mounts ================== */

struct vnode *vfs_root = NULL;

int vfs_mount_root(struct vnode *vnode)
{
    vfs_root = vnode;
    return 0;
}

/**
 * the filesystem whose root is `root' is mounted on directory
 * `dir' (which the mount keeps: a reference); on the root, it is the root
 */
int vfs_bind(struct vnode *dir, struct vnode *root)
{
    if (!dir || !root)
        return -EINVAL;

    if (dir == vfs_root)
        return vfs_mount_root(root);

    if (dir->mounted || root->covered)
        return -EBUSY;

    dir->ref++;
    dir->mounted  = root;
    root->covered = dir;

    return 0;
}

void vfs_init(void)
{
    vfs_log(LOG_INFO, "initializing\n");
}

/**
 * register a new filesystem handler
 */
int vfs_install(struct fs *fs)
{
    struct fs_list *node = kmalloc(sizeof(struct fs_list), &M_FS_LIST, 0);
    if (!node) return -ENOMEM;

    node->name = fs->name;
    node->fs   = fs;

    node->next = registered_fs;
    registered_fs = node;

    vfs_log(LOG_INFO, "registered filesystem %s\n", fs->name);

    return 0;
}

/* ================== VFS high level mappings ================== */

/** check `mode' (VFS_R, VFS_W, VFS_X) access to vnode by the user of uio */
int vfs_access(struct vnode *vnode, struct uio *uio, int mode)
{
    /* root may do anything, but only execute files that have an execute bit */
    if (uio->uid == 0) {
        if ((mode & VFS_X) && !S_ISDIR(vnode->mode) && !(vnode->mode & 0111))
            return -EACCES;

        return 0;
    }

    /* the owner's permissions apply to the owner, then the group's */
    int shift = vnode->uid == uio->uid ? 6 : vnode->gid == uio->gid ? 3 : 0;
    int allowed = (vnode->mode >> shift) & 7;

    return (mode & ~allowed) ? -EACCES : 0;
}

int vfs_perms_check(struct file *file, struct uio *uio)
{
    int mode = 0;
    int accmode = file->flags & O_ACCMODE;

    if (accmode == O_RDONLY || accmode == O_RDWR)
        mode |= VFS_R;

    if (file->flags & (O_WRONLY | O_RDWR))
        mode |= VFS_W;

    if (file->flags & O_EXEC)
        mode |= VFS_X;

    return vfs_access(file->vnode, uio, mode);
}
