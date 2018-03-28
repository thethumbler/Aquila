#ifndef _DEVFS_H
#define _DEVFS_H

#include <fs/vfs.h>

extern struct fs devfs;
extern struct inode *devfs_root;
extern struct vnode vdevfs_root;

#endif /* !_DEVFS_H */
