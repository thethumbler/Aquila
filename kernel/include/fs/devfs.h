#ifndef _DEVFS_H
#define _DEVFS_H

#include <fs/vfs.h>

struct devfs_dir
{
	struct inode     *node;
	struct devfs_dir *next;
};

extern struct fs devfs;
extern struct inode *dev_root;
extern struct vnode vdev_root;

#endif /* !_DEVFS_H */
