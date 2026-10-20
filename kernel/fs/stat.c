#include <core/system.h>
#include <fs/vfs.h>
#include <fs/stat.h>

int vfs_stat(struct vnode *vnode, struct aquila_stat *buf)
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
    buf->st_mtim.tv_sec  = vnode->mtime.tv_sec;
    buf->st_mtim.tv_nsec = vnode->mtime.tv_nsec;
    buf->st_atim.tv_sec  = vnode->atime.tv_sec;
    buf->st_atim.tv_nsec = vnode->atime.tv_nsec;
    buf->st_ctim.tv_sec  = vnode->ctime.tv_sec;
    buf->st_ctim.tv_nsec = vnode->ctime.tv_nsec;

    /* filesystems don't report their allocation, count the size in
     * 512-byte blocks */
    buf->st_blksize = 512;
    buf->st_blocks  = (vnode->size + 511) / 512;

    return 0;
}

