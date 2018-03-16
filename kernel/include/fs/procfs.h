#ifndef _PROCFS_H
#define _PROCFS_H

#include <core/system.h>
#include <fs/vfs.h>

extern struct fs procfs;
extern struct inode *procfs_root;

#endif
