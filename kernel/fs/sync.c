/** \file
 * Syncing vnodes and filesystems to their disks. None of it is
 * implemented yet: writes go to the disk (through the block cache) when
 * they are made.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <aquila/errno.h>

/**
 * Would write a vnode's data and metadata to its disk. Not implemented.
 *
 * @param vnode  the vnode
 * @param mode   what to sync
 * @return `-ENOTSUP`
 */
int vfs_vsync(struct vnode *vnode, int mode)
{
    return -ENOTSUP;
}

/**
 * Would write a filesystem's data and metadata to its disk. Not
 * implemented.
 *
 * @param super  the filesystem's root vnode
 * @param mode   what to sync
 * @return `-ENOTSUP`
 */
int vfs_fssync(struct vnode *super, int mode)
{
    return -ENOTSUP;
}

/**
 * Would write every filesystem's data and metadata to its disk. Not
 * implemented.
 *
 * @param mode  what to sync
 * @return `-ENOTSUP`
 */
int vfs_sync(int mode)
{
    return -ENOTSUP;
}
