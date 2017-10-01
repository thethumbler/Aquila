/*
 *			VFS => File System binding for CPIO archives
 *
 *
 *	This file is part of Aquila OS and is released under
 *	the terms of GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <mm/mm.h>

#include <dev/ramdev.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <fs/devfs.h>

#include <boot/boot.h>

struct fs_node *ramdisk_dev_node = NULL;

void load_ramdisk(module_t *rd_module)
{
	/* Ramdisk is the first module */
	void *ramdisk = (void *) rd_module->addr;
	size_t ramdisk_size = rd_module->size;

	printk("[0] Kernel: Ramdisk %p [%d]\n", ramdisk, ramdisk_size);

	ramdev_private_t *p = kmalloc(sizeof(ramdev_private_t));
	*p = (ramdev_private_t){.addr = ramdisk};

	ramdisk_dev_node = kmalloc(sizeof(struct fs_node));

	*ramdisk_dev_node = (struct fs_node) {
		.name = "ramdisk",
		.type = FS_DIR,
		.fs   = &devfs,
		.dev  = &ramdev,
		.size = ramdisk_size,
		.p    = p,
	};

	struct fs_node *root = initramfs.load(ramdisk_dev_node);
	
	if (!root)
		panic("Could not load ramdisk\n");

	vfs.mount_root(root);
}

static
struct fs_node *new_node(char *name, enum fs_node_type type, 
	size_t sz, size_t data, struct fs_node *sp)
{
	struct fs_node *node = kmalloc(sizeof(struct fs_node));
	
	*node = (struct fs_node) {
		.name = name,
		.size = sz,
		.type = type,
		.fs   = &initramfs,
		.dev  = NULL,
		.p    = kmalloc(sizeof(cpiofs_private_t))
	};

	cpiofs_private_t *p = node->p;

	*p = (cpiofs_private_t) {
		.super  = sp,
  		.parent = NULL,
  		.dir    = NULL,
	  	.count  = 0,
	  	.data   = data,
	  	.next   = NULL
	};

	return node;
}

static struct fs_node *cpiofs_new_child_node(struct fs_node *parent, struct fs_node *child)
{
	if (!parent || !child)	/* Invalid inode */
		return NULL;

	if (parent->type != FS_DIR) /* Adding child to non directory parent */
		return NULL;

	struct fs_node *tmp = ((cpiofs_private_t*)parent->p)->dir;
	((cpiofs_private_t *) child->p)->next = tmp;
	((cpiofs_private_t *) parent->p)->dir = child;

	((cpiofs_private_t *) parent->p)->count++;
	((cpiofs_private_t *) child->p)->parent = parent;

	return child;
}

static struct fs_node *cpiofs_find(struct fs_node *root, const char *path)
{
	char **tokens = tokenize(path, '/');

	if (root->type != FS_DIR)	/* Not even a directory */
		return NULL;

	struct fs_node *cur = root;
	struct fs_node *dir = ((cpiofs_private_t *) cur->p)->dir;

	if (!dir) {	/* Directory has no children */
		if (*tokens == NULL)
			return root;
		else
            return NULL;
	}

	int flag;
	foreach (token, tokens) {
		flag = 0;
		forlinked (e, dir, ((cpiofs_private_t *) e->p)->next) {
			if (e->name && !strcmp(e->name, token)) {
				cur = e;
				dir = ((cpiofs_private_t *) e->p)->dir;
				flag = 1;
				goto next;
			}
		}

		next:

		if (!flag)	/* No such file or directory */
			return NULL;

		continue;
	}

	free_tokens(tokens);

	return cur;
}

static struct fs_node *cpiofs_load(struct fs_node *node)
{
	/* Allocate the root node */
	struct fs_node *rootfs = new_node(NULL, FS_DIR, 0, 0, node);

	cpio_hdr_t cpio;
	size_t offset = 0;
	size_t size = 0;

	for (; offset < node->size; 
		  offset += sizeof(cpio_hdr_t) + (cpio.namesize+1)/2*2 + (size+1)/2*2) {
		size_t data_offset = offset;

		node->fs->read(node, data_offset, sizeof(cpio_hdr_t), &cpio);

		if (cpio.magic != CPIO_BIN_MAGIC) { /* Invalid CPIO archive */
			printk("Invalid CPIO archive\n");
			return NULL;
		}

		size = cpio.filesize[0] * 0x10000 + cpio.filesize[1];
		
		data_offset += sizeof(cpio_hdr_t);
		
		char path[cpio.namesize];
		node->fs->read(node, data_offset, cpio.namesize, path);

		if (!strcmp(path, ".")) continue;
		if (!strcmp(path, "TRAILER!!!")) break;	/* End of archive */

		char *dir = NULL;
		char *name = NULL;

		for (int i = cpio.namesize - 1; i >= 0; --i) {
			if (path[i] == '/') {
				path[i] = '\0';
				name = &path[i+1];
				dir  = path;
				break;
			}
		}

		if (!name) {
			name = path;
			dir  = "/";
		}
		
		data_offset += cpio.namesize + (cpio.namesize % 2);

		struct fs_node *_node = new_node(strdup(name), 
            ((cpio.mode & 0170000 ) == 0040000) ? FS_DIR : FS_FILE,
            size,
            data_offset,
            node
        );

		struct fs_node *parent = cpiofs_find(rootfs, dir);

		cpiofs_new_child_node(parent, _node);
	}

	return rootfs;
}

static ssize_t cpiofs_read(struct fs_node *node, off_t offset, size_t len, void *buf_p)
{
    cpiofs_private_t *p = node->p;
    struct fs_node *super = p->super;
    return super->fs->read(super, p->data + offset, len, buf_p);
}

static ssize_t cpiofs_readdir(struct fs_node *node, off_t offset, struct dirent *dirent)
{
    if ((size_t) offset == ((cpiofs_private_t *) node->p)->count)
        return 0;

    int i = 0;
    struct fs_node *dir = ((cpiofs_private_t *) node->p)->dir;

    forlinked (e, dir, ((cpiofs_private_t *) e->p)->next) {
        if (i == offset) {
            strcpy(dirent->d_name, e->name);   // FIXME
            break;
        }
        ++i;
    }

    return i == offset;
}

static int cpiofs_eof(struct file *file)
{
    if (file->node->type == FS_DIR) {
        return (size_t) file->offset >= ((cpiofs_private_t *) file->node->p)->count;
    } else {
        return (size_t) file->offset > file->node->size;
    }
}

struct fs initramfs = {
	.name = "initramfs",
	.load = &cpiofs_load,
	.find = &cpiofs_find,
	.read = &cpiofs_read,
    .readdir = &cpiofs_readdir,
	//.write = NULL,
	
	.f_ops = {
		.open  = generic_file_open,
		.read  = generic_file_read,
		.write = generic_file_write,
        .readdir = generic_file_readdir,

		.eof = cpiofs_eof,
	},
};
