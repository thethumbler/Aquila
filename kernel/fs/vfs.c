#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <bits/fcntl.h>
#include <bits/errno.h>

inode_t *vfs_root = NULL;

static void vfs_mount_root(inode_t *node)
{
	vfs_root = node;
}

static inode_t *vfs_find(inode_t *inode, const char *path)
{
	/* if path is NULL pointer, or path is empty string, return NULL */
	if(!path ||  !*path)
		return NULL;

	if(!inode)
		inode = vfs_root;

	/* Tokenize slash seperated words in path into tokens */
	char **tokens = tokenize(path, '/');

	inode_t *cur = inode;

	foreach(token, tokens)
	{
		if(cur->mountpoint)
			cur = cur->mountpoint;
		
		cur = cur->fs->find(cur, token);

		if(!cur) return NULL;
	}

	free_tokens(tokens);

	return cur;
}

static size_t vfs_read(inode_t *inode, size_t offset, size_t size, void *buf)
{
	if(!inode) return 0;
	return inode->fs->read(inode, offset, size, buf);
}

static size_t vfs_write(inode_t *inode, size_t offset, size_t size, void *buf)
{
	if(!inode) return 0;
	return inode->fs->write(inode, offset, size, buf);
}

static inode_t *vfs_create(inode_t *dir, const char *name)
{
	if(dir->type != FS_DIR)
		return NULL;

	return dir->fs->create(dir, name);
}

static inode_t *vfs_mkdir(inode_t *dir, const char *name)
{
	if(dir->type != FS_DIR)
		return NULL;

	return dir->fs->mkdir(dir, name);
}

static int vfs_mount(inode_t *parent, inode_t *child)
{
	parent->mountpoint = child;
	return 0;
}

static int vfs_ioctl(inode_t *file, unsigned long request, void *argp)
{
	if(!file || !file->fs || !file->fs->ioctl)
		return -1;
	return file->fs->ioctl(file, request, argp);
}

/*static int vfs_open(inode_t *file, int flags)
{
	if(!file || !file->fs || !file->fs->open)
		return -1;
	return file->fs->open(file, flags);
}*/


/* VFS Generic File function */

int vfs_generic_file_open(fd_t *fd __unused)
{
	return 0;
}

size_t vfs_generic_file_read(fd_t *fd, void *buf, size_t size)
{
	if(fd->flags & O_NONBLOCK)	/* Non-blocking I/O */
	{
		size_t read = fd->inode->fs->read(fd->inode, fd->offset, size, buf);
		
		if(read == 0)	/* No data available -- FIXME handle EOF */
			return -EAGAIN;

		fd->offset += read;
		if(fd->inode->write_queue)
			wakeup_queue(fd->inode->write_queue);	/* Wakeup all sleeping writers */
		return read;
	} else	/* Blocking I/O */
	{
		size_t retval = size;
		while(size)
		{
			size -= fd->inode->fs->read(fd->inode, fd->offset, size, buf);
			if(!size)
				break;

			sleep_on(fd->inode->read_queue);
		}
		
		fd->offset += retval;
		if(fd->inode->write_queue)
			wakeup_queue(fd->inode->write_queue);	/* Wakeup all sleeping writers */
		return retval;
	}
}

size_t vfs_generic_file_write(fd_t *fd, void *buf, size_t size)
{
	if(fd->flags & O_NONBLOCK)	/* Non-blocking I/O */
	{
		size_t written = fd->inode->fs->write(fd->inode, fd->offset, size, buf);
		
		if(written == 0)	/* Can't write data */
			return -EAGAIN;

		fd->offset += written;
		if(fd->inode->read_queue)
			wakeup_queue(fd->inode->read_queue);	/* Wakeup all sleeping readers */
		return written;
	} else	/* Blocking I/O */
	{
		size_t retval = size;
		while(size)
		{
			size -= fd->inode->fs->write(fd->inode, fd->offset, size, buf);
			if(!size)
				break;

			sleep_on(fd->inode->write_queue);
		}

		fd->offset += retval;
		if(fd->inode->read_queue)
			wakeup_queue(fd->inode->read_queue);	/* Wakeup all sleeping readers */
		return retval;
	}
}

struct vfs vfs = (struct vfs) 
{
	.mount_root = &vfs_mount_root,
	.create = &vfs_create,
	.mkdir = &vfs_mkdir,
	//.open = &vfs_open,
	.mount = &vfs_mount,
	.read = &vfs_read,
	.write = &vfs_write,
	.ioctl = &vfs_ioctl,
	.find = &vfs_find,
};