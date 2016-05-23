#ifndef _DEVFS_H
#define _DEVFS_H

#include <fs/vfs.h>

typedef struct devfs_dir
{
	inode_t *inode;
	struct devfs_dir *next;
} devfs_dir_t;

extern fs_t devfs;
extern inode_t *dev_root;

void devfs_init();

#endif /* !_DEVFS_H */