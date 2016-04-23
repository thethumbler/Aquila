#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>

static inode_t *vfs_root = NULL;

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
		/*if(tmp->type == FS_MOUNTPOINT)
			tmp = ((vfs_mountpoint_t*)tmp->p)->inode;
		*/
		cur = cur->fs->find(cur, token);
		if(!cur) return NULL;
	}

	return cur;
}

static size_t vfs_read(inode_t *inode, size_t offset, size_t size, void *buf)
{
	if(!inode) return 0;
	return inode->fs->read(inode, offset, size, buf);
}

struct vfs vfs = (struct vfs) 
{
	.mount_root = &vfs_mount_root,
	.read = &vfs_read,
	.find = &vfs_find,
};