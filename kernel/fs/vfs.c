#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>

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

static inode_t *vfs_create(inode_t *dir, const char *name)
{
	if(dir->type != FS_DIR)
		return NULL;

	return dir->fs->create(dir, name);
}

static int vfs_mount(inode_t *parent, inode_t *child)
{
	parent->mountpoint = child;
	return 0;
}

static int vfs_open(inode_t *file, int flags)
{
	if(!file || !file->fs || !file->fs->open)
		return -1;
	return file->fs->open(file, flags);
}

struct vfs vfs = (struct vfs) 
{
	.mount_root = &vfs_mount_root,
	.create = &vfs_create,
	.open = &vfs_open,
	.mount = &vfs_mount,
	.read = &vfs_read,
	.find = &vfs_find,
};