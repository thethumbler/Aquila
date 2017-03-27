/**********************************************************************
 *					Device Filesystem (devfs) handler
 *
 *
 *	This file is part of Aquila OS and is released under the terms of
 *	GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>

#include <mm/mm.h>

#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>

#include <bits/errno.h>

/* devfs root directory (usually mounted on '/dev') */
struct fs_node *dev_root = NULL;

static ssize_t devfs_read(struct fs_node *node, off_t offset, size_t size, void *buf)
{
	if (!node->dev)	/* is node connected to a device handler? */
		return -EINVAL;

	return node->dev->read(node, offset, size, buf);
}

static ssize_t devfs_write(struct fs_node *node, off_t offset, size_t size, void *buf)
{
	if (!node->dev)	/* is node connected to a device handler? */
		return -EINVAL;

	return node->dev->write(node, offset, size, buf);
}

static struct fs_node *devfs_create(struct fs_node *dir, const char *name)
{
	struct fs_node *node = kmalloc(sizeof(struct fs_node));
	memset(node, 0, sizeof(struct fs_node));
	
	node->name = strdup(name);
	node->type = FS_FILE;
	node->fs   = &devfs;
	node->size = 0;

	struct devfs_dir *_dir = (struct devfs_dir *) dir->p;
	struct devfs_dir *tmp  = kmalloc(sizeof(*tmp));

	tmp->next = _dir;
	tmp->node = node;

	dir->p = tmp;

	return node;
}

static struct fs_node *devfs_mkdir(struct fs_node *parent, const char *name)
{
	struct fs_node *d = devfs_create(parent, name);

	d->type = FS_DIR;

	return d;
}

static int devfs_ioctl(struct fs_node *file, unsigned long request, void *argp)
{
	if(!file || !file->dev || !file->dev->ioctl)
		return -EBADFD;

	return file->dev->ioctl(file, request, argp);
}

static struct fs_node *devfs_find(struct fs_node *dir, const char *fn)
{
	if (dir->type != FS_DIR)
		return NULL;

	struct devfs_dir *_dir = (struct devfs_dir *) dir->p;

	if (!_dir)	/* Directory not initialized */
		return NULL;

	forlinked (file, _dir, file->next) {
		if (!strcmp(file->node->name, fn))
			return file->node;
	}

	return NULL;	/* File not found */
}


/* ================ File Operations ================ */

static int devfs_file_open(struct file *file)
{
	if (!file->node->dev)
		return -ENXIO;
	
	return file->node->dev->f_ops.open(file);
}

static ssize_t devfs_file_read(struct file *file, void *buf, size_t size)
{
	if (!file->node->dev)
		return -ENXIO;

	return file->node->dev->f_ops.read(file, buf, size);
}

static ssize_t devfs_file_write(struct file *file, void *buf, size_t size)
{
	if (!file->node->dev)
		return -ENXIO;
	
	return file->node->dev->f_ops.write(file, buf, size);
}

static int devfs_file_can_read(struct file *file, size_t size)
{
	if (!file->node->dev)
		return -ENXIO;

	return file->node->dev->f_ops.can_read(file, size);
}

static int devfs_file_can_write(struct file *file, size_t size)
{
	if (!file->node->dev)
		return -ENXIO;

	return file->node->dev->f_ops.can_write(file, size);
}

static int devfs_file_eof(struct file *file)
{
	if (!file->node->dev)
		return -ENXIO;

	return file->node->dev->f_ops.eof(file);
}

void devfs_init()
{
    printk("[0] Kernel: devfs -> init()\n");
	dev_root = kmalloc(sizeof(struct fs_node));

	dev_root->name = "dev";
	dev_root->type = FS_DIR;
	dev_root->size = 0;
	dev_root->fs   = &devfs;
	dev_root->p    = NULL;
}

struct fs devfs = {
	.name   = "devfs",
	.create = devfs_create,
	.mkdir  = devfs_mkdir,
	.find   = devfs_find,
	.read   = devfs_read,
	.write  = devfs_write,
	.ioctl  = devfs_ioctl,
	
	.f_ops = {
		.open  = devfs_file_open,
		.read  = devfs_file_read,
		.write = devfs_file_write, 

        .can_read = devfs_file_can_read,
        .can_write = devfs_file_can_write,
		.eof = devfs_file_eof,
	},
};
