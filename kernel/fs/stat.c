#include <core/system.h>
#include <fs/vfs.h>
#include <fs/stat.h>

int vfs_stat(struct inode *inode, struct stat *buf)
{
    buf->st_dev   = inode->dev;
    buf->st_ino   = inode->ino;
    buf->st_mode  = inode->mode;
    buf->st_nlink = inode->nlink;
    buf->st_uid   = inode->uid;
    buf->st_gid   = inode->gid;
    buf->st_rdev  = inode->rdev;
    buf->st_size  = inode->size;
    buf->st_mtime = inode->mtime;

    return 0;
}

