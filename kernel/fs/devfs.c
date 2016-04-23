#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>

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

fs_t devfs = 
{
	.name = "devfs",
	.read = &devfs_read,
	.write = &devfs_write,
};