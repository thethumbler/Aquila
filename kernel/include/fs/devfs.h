#ifndef _DEVFS_H
#define _DEVFS_H

#include <fs/vfs.h>

struct devfs_dir
{
	struct fs_node   *node;
	struct devfs_dir *next;
};

extern struct fs devfs;
extern struct fs_node *dev_root;

#endif /* !_DEVFS_H */
