#ifndef _FS_DEVPTS_H
#define _FS_DEVPTS_H

#include <fs/vfs.h>

/* devpts, the filesystem of /dev/pts (fs/devpts/): the pseudo-terminals'
 * slaves. And its root. */
extern struct fs devpts;
extern struct vnode *devpts_root;

#endif /* ! _FS_DEVPTS_H */
