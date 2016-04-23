#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>

typedef struct filesystem fs_t;
typedef enum {FS_FILE, FS_DIR, FS_CHRDEV, FS_BLKDEV, FS_SYMLINK, FS_PIPE, FS_MOUNTPOINT} inode_type;
typedef struct inode inode_t;
typedef struct dentry dentry_t;

#include <dev/dev.h>

struct filesystem
{
	char		*name;	
	inode_t*	(*load) (inode_t *inode);
	size_t 		(*read) (inode_t *inode, size_t offset, size_t size, void *buf);
	size_t 		(*write)(inode_t *inode, size_t offset, size_t size, void *buf);
	int 		(*ioctl)(inode_t *inode, unsigned long, ...);

	inode_t*	(*find)(inode_t *dir, const char *name);
};

struct inode
{
	char		*name;
	size_t		size;
	inode_type	type;
	fs_t 		*fs;
	dev_t		*dev;
	void 		*p;		/* Filesystem handler private data */
};

struct vfs
{
	void		(*mount_root) (inode_t *inode);
	size_t 		(*read) (inode_t *inode, size_t offset, size_t size, void *buf);
	size_t 		(*write)(inode_t *inode, size_t offset, size_t size, void *buf);
	int 		(*ioctl)(inode_t *inode, unsigned long, ...);

	inode_t*	(*find) (inode_t *dir, const char *name);
};

extern struct vfs vfs;

#endif /* !_VFS_H */