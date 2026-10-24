#ifndef _FS_DEVFS_H
#define _FS_DEVFS_H

#include <fs/vfs.h>

/* devfs, the filesystem of /dev (fs/devfs/), and its root. */
extern struct fs devfs;
extern struct vnode *devfs_root;

#endif /* ! _FS_DEVFS_H */
