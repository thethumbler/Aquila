#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <bits/fcntl.h>
#include <bits/errno.h>

struct fs_node *vfs_root = NULL;

static void vfs_mount_root(struct fs_node *node)
{
	vfs_root = node;
}

static struct fs_node *vfs_find(struct fs_node *inode, const char *path)
{
	/* if path is NULL pointer, or path is empty string, return NULL */
	if(!path ||  !*path)
		return NULL;

	if(!inode)
		inode = vfs_root;

	/* Tokenize slash seperated words in path into tokens */
	char **tokens = tokenize(path, '/');

	struct fs_node *cur = inode;

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

static size_t vfs_read(struct fs_node *inode, size_t offset, size_t size, void *buf)
{
	if(!inode) return 0;
	return inode->fs->read(inode, offset, size, buf);
}

static size_t vfs_write(struct fs_node *inode, size_t offset, size_t size, void *buf)
{
	if(!inode) return 0;
	return inode->fs->write(inode, offset, size, buf);
}

static struct fs_node *vfs_create(struct fs_node *dir, const char *name)
{
	if(dir->type != FS_DIR)
		return NULL;

	return dir->fs->create(dir, name);
}

static struct fs_node *vfs_mkdir(struct fs_node *dir, const char *name)
{
	if(dir->type != FS_DIR)
		return NULL;

	return dir->fs->mkdir(dir, name);
}

static int vfs_mount(struct fs_node *parent, struct fs_node *child)
{
	parent->mountpoint = child;
	return 0;
}

static int vfs_ioctl(struct fs_node *file, unsigned long request, void *argp)
{
	if(!file || !file->fs || !file->fs->ioctl)
		return -1;
	return file->fs->ioctl(file, request, argp);
}

/*static int vfs_open(struct fs_node *file, int flags)
{
	if(!file || !file->fs || !file->fs->open)
		return -1;
	return file->fs->open(file, flags);
}*/


/* VFS Generic File function */
int generic_file_open(struct file * file __unused)
{
	return 0;
}

struct vfs vfs = (struct vfs) {
	.mount_root = &vfs_mount_root,
	.create = &vfs_create,
	.mkdir = &vfs_mkdir,
	.mount = &vfs_mount,
	.read = &vfs_read,
	.write = &vfs_write,
	.ioctl = &vfs_ioctl,
	.find = &vfs_find,
};