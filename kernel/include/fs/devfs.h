#ifndef _FS_DEVFS_H
#define _FS_DEVFS_H

#include <fs/vfs.h>

extern struct fs devfs;
extern struct inode *devfs_root;
extern struct vnode vdevfs_root;

#endif /* ! _FS_DEVFS_H */
