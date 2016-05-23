#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>

#include <boot/boot.h>
#include <fs/devfs.h>
#include <dev/ramdev.h>

void load_ramdisk()
{
	/* Ramdisk is the first module */
	void *ramdisk = (void*) kernel_modules[0].addr;
	size_t ramdisk_size = kernel_modules[0].size;

	//printk("ramdisk %x [%d]\n", ramdisk, ramdisk_size);

	ramdev_private_t *p = kmalloc(sizeof(ramdev_private_t));
	*p = (ramdev_private_t){.addr = ramdisk};

	inode_t *node = kmalloc(sizeof(inode_t));

	*node = (inode_t)
	{
		.name = "ram0",
		.type = FS_DIR,
		.fs   = &devfs,
		.dev  = &ramdev,
		.size = ramdisk_size,
		.p    = p,
	};

	inode_t *root = initramfs.load(node);
	if(!root)
		panic("Could not load ramdisk\n");

	vfs.mount_root(root);
}

static
inode_t *new_node(char *name, inode_type type, 
	size_t sz, size_t data, inode_t *sp)
{
	inode_t *node = kmalloc(sizeof(inode_t));
	
	*node = (inode_t)
	{
		.name = name,
		.size = sz,
		.type = type,
		.fs   = &initramfs,
		.dev  = NULL,
		.p    = kmalloc(sizeof(cpiofs_private_t))
	};

	cpiofs_private_t *p = node->p;

	*p = (cpiofs_private_t)
	{
		.super  = sp,
  		.parent = NULL,
  		.dir    = NULL,
	  	.count  = 0,
	  	.data   = data,
	  	.next   = NULL
	};

	return node;
}

static inode_t *cpiofs_new_child_node(inode_t *parent, inode_t *child)
{
	if(!parent || !child)	/* Invalid inode */
		return NULL;

	if(parent->type != FS_DIR) /* Adding child to non directory parent */
		return NULL;

	inode_t *tmp = ((cpiofs_private_t*)parent->p)->dir;
	((cpiofs_private_t*)child->p)->next = tmp;
	((cpiofs_private_t*)parent->p)->dir = child;

	((cpiofs_private_t*)parent->p)->count++;
	((cpiofs_private_t*)child->p)->parent = parent;

	return child;
}

static inode_t *cpiofs_find(inode_t *root, const char *path)
{
	char **tokens = tokenize(path, '/');

	if(root->type != FS_DIR)	/* Not even a directory */
		return NULL;

	inode_t *cur = root;
	inode_t *dir = ((cpiofs_private_t*)cur->p)->dir;

	if(!dir)	/* Directory has no children */
	{
		if(*tokens == '\0')
			return root;
		else return NULL;
	}

	int flag;
	foreach(token, tokens)
	{
		flag = 0;
		forlinked(e, dir, ((cpiofs_private_t*)e->p)->next)
		{
			if(e->name && !strcmp(e->name, token))
			{
				cur = e;
				dir = ((cpiofs_private_t*)e->p)->dir;
				flag = 1;
				goto next;
			}
		}

		next:

		if(!flag)	/* No such file or directory */
			return NULL;

		continue;
	}

	free_tokens(tokens);

	return cur;
}

static inode_t *cpiofs_load(inode_t *inode)
{
	/* Allocate the root node */
	inode_t *rootfs = new_node(NULL, FS_DIR, 0, 0, inode);

	cpio_hdr_t cpio;
	size_t offset = 0;
	size_t size = 0;

	for(; offset < inode->size; 
		  offset += sizeof(cpio_hdr_t) + (cpio.namesize+1)/2*2 + (size+1)/2*2)
	{
		size_t data_offset = offset;

		inode->fs->read(inode, data_offset, sizeof(cpio_hdr_t), &cpio);

		if(cpio.magic != CPIO_BIN_MAGIC) /* Invalid CPIO archive */
			return NULL;

		size = cpio.filesize[0] * 0x10000 + cpio.filesize[1];
		
		data_offset += sizeof(cpio_hdr_t);
		
		char path[cpio.namesize];
		inode->fs->read(inode, data_offset, cpio.namesize, path);

		if(!strcmp(path, ".")) continue;
		if(!strcmp(path, "TRAILER!!!")) break;	/* End of archive */

		char *dir = NULL;
		char *name = NULL;

		for(int i = cpio.namesize - 1; i >= 0; --i)
		{
			if(path[i] == '/')
			{
				path[i] = '\0';
				name = &path[i+1];
				dir  = path;
				break;
			}
		}

		if(!name)
		{
			name = path;
			dir  = "/";
		}
		
		data_offset += cpio.namesize + (cpio.namesize % 2);

		inode_t *node = 
			new_node(strdup(name), 
				((cpio.mode & 0170000 ) == 0040000) ? FS_DIR : FS_FILE,
				size,
				data_offset,
				inode
				);

		inode_t *parent = cpiofs_find(rootfs, dir);

		cpiofs_new_child_node(parent, node);
	}

	return rootfs;
}

static size_t cpiofs_read(inode_t *inode, size_t offset, size_t len, void *buf_p)
{
	cpiofs_private_t *p = inode->p;

	inode_t *super = p->super;

	return super->fs->read(super, p->data + offset, len, buf_p);
}

fs_t initramfs = 
(fs_t)
{
	.name = "initramfs",
	.load = &cpiofs_load,
	.find = &cpiofs_find,
	.read = &cpiofs_read,
	//.write = NULL,
};
