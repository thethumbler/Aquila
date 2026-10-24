/** \file
 * `ioctl()` on a vnode: passed on to its device or its filesystem.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

/**
 * Passes an `ioctl()` request to a vnode's device driver, or to its
 * filesystem.
 *
 * @param vnode    the vnode
 * @param request  the request
 * @param argp     its argument, often a pointer to user memory
 * @return the request's result, `-EINVAL` for an invalid vnode, or
 *         `-ENOSYS` if its filesystem has no ioctl operation
 */
int vfs_ioctl(struct vnode *vnode, unsigned long request, void *argp)
{
    vfs_log(LOG_DEBUG, "vfs_ioctl(vnode=%p, request=%ld, argp=%p)\n", vnode, request, argp);

    /* invalid request */
    if (!vnode)
        return -EINVAL;

    /* a device node: the driver handles it */
    if (ISDEV(vnode))
        return kdev_ioctl(&VNODE_DEV(vnode), request, argp);

    /* invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* the filesystem doesn't support it */
    if (!vnode->fs->vops.ioctl)
        return -ENOSYS;

    return vnode->fs->vops.ioctl(vnode, request, argp);
}

