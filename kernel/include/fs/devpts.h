#ifndef _FS_DEVPTS_H
#define _FS_DEVPTS_H

#include <fs/vfs.h>

extern struct fs devpts;
extern struct inode *devpts_root;
extern struct vnode vdevpts_root;

#endif /* ! _FS_DEVPTS_H */
