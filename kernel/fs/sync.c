#include <core/system.h>
#include <fs/vfs.h>
#include <aquila/errno.h>

/**
 * sync the metadata and/or data associated with a vnode
 */
int vfs_vsync(struct vnode *vnode, int mode)
{
    return -ENOTSUP;
}

/**
 * sync the metadata and/or data associated with a filesystem
 */
int vfs_fssync(struct vnode *super, int mode)
{
    return -ENOTSUP;
}

/**
 * sync all metadata and/or data of all filesystems
 */
int vfs_sync(int mode)
{
    return -ENOTSUP;
}
