#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>

typedef struct filesystem fs_t;
typedef enum {FS_FILE, FS_DIR, FS_CHRDEV, FS_BLKDEV, FS_SYMLINK, FS_PIPE} inode_type;
typedef struct inode inode_t;
typedef struct dentry dentry_t;

#include <dev/dev.h>

struct filesystem
{
	char		*name;
	inode_t*	(*load) (inode_t *inode);
	int			(*open)(inode_t *file, int flags);
	inode_t*	(*create)(inode_t *dir, const char *name);
	inode_t*	(*mkdir)(inode_t *dir, const char *name);
	size_t 		(*read) (inode_t *inode, size_t offset, size_t size, void *buf);
	size_t 		(*write)(inode_t *inode, size_t offset, size_t size, void *buf);
	int 		(*ioctl)(inode_t *inode, unsigned long request, void *argp);

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

	inode_t 	*mountpoint;	/* Mount point to another inode */
};

struct vfs
{
	void		(*mount_root) (inode_t *inode);
	inode_t*	(*create)(inode_t *dir, const char *name);
	inode_t*	(*mkdir) (inode_t *dir, const char *name);
	int			(*open)(inode_t *file, int flags);
	size_t 		(*read) (inode_t *inode, size_t offset, size_t size, void *buf);
	size_t 		(*write)(inode_t *inode, size_t offset, size_t size, void *buf);
	int 		(*ioctl)(inode_t *inode, unsigned long request, void *argp);
	int 		(*mount)(inode_t *parent, inode_t *child);

	inode_t*	(*find) (inode_t *dir, const char *name);
};

typedef struct 
{
	inode_t *inode;
	inode_type type;
	void 	*p;
} vfs_mountpoint_t;

extern struct vfs vfs;
extern inode_t *vfs_root;
int vfs_generic_open(inode_t *file, int flags);

#endif /* !_VFS_H */