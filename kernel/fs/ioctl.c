#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

int vfs_ioctl(struct vnode *vnode, unsigned long request, void *argp)
{
    vfs_log(LOG_DEBUG, "vfs_ioctl(vnode=%p, request=%ld, argp=%p)\n", vnode, request, argp);

    /* TODO Basic ioctl handling */

    /* Invalid request */
    if (!vnode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(vnode))
        return kdev_ioctl(&VNODE_DEV(vnode), request, argp);

    /* Invalid request */
    if (!vnode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!vnode->fs->vops.ioctl)
        return -ENOSYS;

    return vnode->fs->vops.ioctl(vnode, request, argp);
}

