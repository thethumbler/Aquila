/** \file
 * The virtual filesystem (VFS): the filesystems that are installed, the
 * root, mounting one filesystem on a directory of another, and
 * permissions. See docs/kernel/vfs.md.
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

/** The installed filesystems. */
struct fs_list *registered_fs = NULL;

/* Mounts */

struct vnode *vfs_root = NULL;

/**
 * Makes a vnode the root of the filesystem tree.
 *
 * @param vnode  the root directory of a filesystem
 * @return 0
 */
int vfs_mount_root(struct vnode *vnode)
{
    vfs_root = vnode;
    return 0;
}

/**
 * Mounts a filesystem on a directory: a lookup that reaches `dir` goes
 * on in `root` instead. The mount keeps a reference to `dir`. Mounting
 * on the root makes `root` the new root.
 *
 * @param dir   the directory to mount on
 * @param root  the root directory of the filesystem to mount
 * @return 0, `-EINVAL`, or `-EBUSY` if something is mounted on `dir`
 *         already, or `root` is mounted elsewhere
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

/**
 * Sets up the VFS at boot.
 */
void vfs_init(void)
{
    vfs_log(LOG_INFO, "initializing\n");
}

/**
 * Installs a filesystem, so that it can be mounted by name.
 *
 * @param fs  the filesystem
 * @return 0, or `-ENOMEM`
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

/* Permissions */

/**
 * Checks whether a user may access a vnode in a way: the owner's
 * permissions apply to its owner, the group's to its group, the others'
 * to everyone else. Root may do anything, but only execute a file that
 * has an execute bit.
 *
 * @param vnode  the vnode
 * @param uio    who is asking
 * @param mode   the access: `VFS_R`, `VFS_W` and `VFS_X`, or'ed
 * @return 0 if it's allowed, or `-EACCES`
 */
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

/**
 * Checks whether a user may open a file the way it's being opened (read,
 * write, execute, as its flags say).
 *
 * @param file  the file being opened, its flags set
 * @param uio   who is opening it
 * @return 0 if it's allowed, or `-EACCES`
 */
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
