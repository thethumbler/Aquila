#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <dev/dev.h>
#include <fs/devfs.h>

inode_t *dev_root = NULL;

static size_t devfs_read(inode_t *inode, size_t offset, size_t size, void *buf)
{
	if(!inode->dev)	/* is inode connected to a device handler? */
		return -1;

	return inode->dev->read(inode, offset, size, buf);
}

static size_t devfs_write(inode_t *inode, size_t offset, size_t size, void *buf)
{
	if(!inode->dev)	/* is inode connected to a device handler? */
		return -1;

	return inode->dev->write(inode, offset, size, buf);
}

static inode_t *devfs_create(inode_t *dir, const char *name)
{
	inode_t *inode = kmalloc(sizeof(inode_t));
	memset(inode, 0, sizeof(inode_t));
	
	inode->name = strdup(name);
	inode->type = FS_FILE;
	inode->fs   = &devfs;
	inode->size = 0;

	devfs_dir_t *d = (devfs_dir_t*) dir->p;

	devfs_dir_t *tmp = kmalloc(sizeof(*tmp));

	tmp->next = d;
	tmp->inode = inode;

	dir->p = tmp;

	return inode;
}

static inode_t *devfs_mkdir(inode_t *parent, const char *name)
{
	inode_t *d = devfs_create(parent, name);

	d->type = FS_DIR;

	return d;
}

static int devfs_open(inode_t *file, int flags)
{
	if(!file->dev)
		return 0;
	return file->dev->open(file, flags);
}

static int devfs_ioctl(inode_t *file, unsigned long request, void *argp)
{
	if(!file || !file->dev || !file->dev->ioctl)
		return 0;
	return file->dev->ioctl(file, request, argp);
}

static inode_t *devfs_find(inode_t *dir, const char *fn)
{
	if(dir->type != FS_DIR)
		return NULL;

	devfs_dir_t *d = (devfs_dir_t *) dir->p;
	if(!d)	/* Directory not initialized */
		return NULL;

	forlinked(file, d, file->next)
	{
		if(!strcmp(file->inode->name, fn))
			return file->inode;
	}

	return NULL;	/* File not found */
}

void devfs_init()
{
	dev_root = kmalloc(sizeof(inode_t));

	dev_root->name = "dev";
	dev_root->type = FS_DIR;
	dev_root->size = 0;
	dev_root->fs   = &devfs;
	dev_root->p    = NULL;
}

fs_t devfs = 
{
	.name = "devfs",
	.create = &devfs_create,
	.mkdir = &devfs_mkdir,
	.open = &devfs_open,
	.find = &devfs_find,
	.read = &devfs_read,
	.write = &devfs_write,
	.ioctl = &devfs_ioctl,
};