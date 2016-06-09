#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>

typedef struct filesystem fs_t;
typedef enum {FS_FILE, FS_DIR, FS_CHRDEV, FS_BLKDEV, FS_SYMLINK, FS_PIPE} inode_type;
typedef struct inode inode_t;
typedef struct dentry dentry_t;
typedef struct file_descriptor fd_t;

struct file_ops
{
	int			(*open) (fd_t *fd);
	size_t 		(*read) (fd_t *fd, void *buf, size_t size);	
	size_t 		(*write)(fd_t *fd, void *buf, size_t size);	
} __packed;

#include <dev/dev.h>
#include <sys/proc.h>
#include <ds/queue.h>

struct filesystem
{
	char		*name;
	inode_t*	(*load) (inode_t *inode);
	inode_t*	(*create)(inode_t *dir, const char *name);
	inode_t*	(*mkdir)(inode_t *dir, const char *name);
	size_t 		(*read) (inode_t *inode, size_t offset, size_t size, void *buf);
	size_t 		(*write)(inode_t *inode, size_t offset, size_t size, void *buf);
	int 		(*ioctl)(inode_t *inode, unsigned long request, void *argp);
	inode_t*	(*find)(inode_t *dir, const char *name);

	/* File operations */
	struct file_ops f_ops;
};

struct inode
{
	char		*name;
	size_t		size;
	inode_type	type;
	fs_t 		*fs;
	dev_t		*dev;
	void 		*p;		/* Filesystem handler private data */

	queue_t		*read_queue;
	queue_t 	*write_queue;

	inode_t 	*mountpoint;	/* Mount point to another inode */
};

struct file_descriptor
{
	inode_t *inode;
	size_t  offset;
	int 	flags;
};	/* File descriptor type */

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

/* Generic File Operations */
int vfs_generic_file_open(fd_t *fd);
size_t vfs_generic_file_read(fd_t *fd, void *buf, size_t size);
size_t vfs_generic_file_write(fd_t *fd, void *buf, size_t size);

#endif /* !_VFS_H */