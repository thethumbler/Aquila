#include <core/system.h>
#include <fs/vfs.h>
#include <fs/stat.h>

int vfs_stat(struct vnode *vnode, struct stat *buf)
{
    vfs_log(LOG_DEBUG, "vfs_stat(vnode=%p, buf=%p)\n", vnode, buf);

    buf->st_dev   = vnode->dev;
    buf->st_ino   = vnode->ino;
    buf->st_mode  = vnode->mode;
    buf->st_nlink = vnode->nlink;
    buf->st_uid   = vnode->uid;
    buf->st_gid   = vnode->gid;
    buf->st_rdev  = vnode->rdev;
    buf->st_size  = vnode->size;
    buf->st_mtime = vnode->mtime;
    buf->st_atime = vnode->atime;
    buf->st_ctime = vnode->ctime;

    return 0;
}

