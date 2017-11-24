#include <core/panic.h>
#include <fs/ext2.h>
#include <bits/errno.h>

typedef struct {
    struct fs_node *supernode;
	struct ext2_superblock *superblock;
	uint32_t inode;
} ext2_private_t;

static struct ext2_inode *ext2_get_inode(struct fs_node *node, struct ext2_superblock *sb, uint32_t inode)
{
    printk("ext2_get_inode(node=%p, sb=%p, inode=%d)\n", node, sb, inode);
	uint32_t bs = 1024UL << sb->block_size;
    uint32_t bgd_table = bs == 1024? 2*1024 : bs;
	uint32_t block_group = (inode - 1) / sb->inodes_per_block_group;
	struct ext2_block_group_descriptor *bgd = kmalloc(sizeof(*bgd));
	node->fs->read(node, bgd_table + block_group, sizeof(struct ext2_block_group_descriptor), bgd);

	uint32_t index = (inode - 1) % sb->inodes_per_block_group;
	struct ext2_inode *i = kmalloc(sizeof(struct ext2_inode));

	node->fs->read(node, bgd->inode_table * bs + index * sb->inode_size, sizeof(struct ext2_inode), i);
    kfree(bgd);
	return i;
}

static void *read_block(struct fs_node *supernode, struct ext2_superblock *sb, uint32_t number, void *buf)
{
    printk("read_block(supernode=%p, sb=%p, number=%d, buf=%p)\n", supernode, sb, number, buf);
	uint32_t bs = 1024UL << sb->block_size;
	supernode->fs->read(supernode, number * bs, bs, buf);
	return buf;
}

#if 0
uint32_t *get_block_ptrs(struct fs_node *node, struct ext2_superblock *sb, struct ext2_inode *inode)
{
	uint32_t bs = 1024UL << sb->block_size;

	uint32_t isize = inode->size / bs + (inode->size % bs ? 1 : 0 );
	uint32_t *blks = kmalloc(sizeof(uint32_t) * (isize + 1));

	// The size is always saved as the first entry of the returned array
	blks[0] = isize;

	uint32_t k;
	for(k = 1; k <= 12 && k <= isize; ++k)
		blks[k] = inode->direct_pointer[k - 1];

	if(isize <= 12)	// Was that enough !?
		return blks;

	isize -= 12;


	uint32_t *tmp = get_block(dev_inode, sb, inode->singly_indirect_pointer, kmalloc(bs));

	for(k = 0; k < (bs/4) && k <= isize; ++k)
		blks[13 + k] = tmp[k];

	kfree(tmp);

	if(isize <= (bs/4))	// Are we done yet !?
		return blks;

	isize -= (bs/4);

	tmp = get_block(dev_inode, sb, inode->doubly_indirect_pointer, kmalloc(bs));
	uint32_t *tmp2 = kmalloc(bs);

	uint32_t j;
	for(k = 0; k < (bs/4) && (k * (bs/4)) <= isize; ++k)
	{
		tmp2 = get_block(dev_inode, sb, tmp[k], tmp2);
		for(j = 0; j < (bs/4) && (k * (bs/4) + j) <= isize; ++j)
		{
			blks[13 + bs/4 + k * bs/4 + j] = tmp2[j];
		}
	}

	if(isize <= (bs/4)*(bs/4))
		return blks;

	kfree(tmp);

	// TODO : Support triply indirect pointers
	panic("File size too big\n");
	for (;;);
}
#endif

static struct fs_node *ext2_load(struct fs_node *node)
{
    printk("ext2_load(node=%p [name=%s])\n", node, node->name);

    /* Read Superblock */
	struct ext2_superblock *sb = kmalloc(sizeof(*sb));
	node->fs->read(node, 1024, sizeof(*sb), sb);

    /* Valid Ext2? */
	if (sb->ext2_signature != 0xEF53) {
        kfree(sb);
        return NULL;
    }

    struct fs_node *root = kmalloc(sizeof(struct fs_node));
	root->type = FS_DIR;
	root->size = 0;
	root->fs   = &ext2fs;

    ext2_private_t *p = kmalloc(sizeof(ext2_private_t));
    p->inode = 2;
    p->superblock = sb;
    p->supernode = node;
	root->p    = p;

    return root;

#if 0
	//uint32_t *b = get_block(dev, sb, 2);

	uint32_t bs = 1024UL << sb->block_size;	// Block size
	uint32_t is = sb->inode_size;			// Inode size

	block_group_descriptor_t *bgd = kmalloc(sizeof(*bgd));
	vfs_read(dev_inode, (sb->block_size ? 1 : 2) * bs, sizeof(*bgd), (void*)bgd);

	ext2_inode_t *root_inode = kmalloc(is);
	vfs_read(dev_inode, bs * bgd->inode_table + is, is, root_inode);

	uint32_t *ptrs = get_block_ptrs(dev_inode, sb, root_inode);
	uint32_t count = *ptrs++;

	uint32_t k;

	ext2_dentry_t *_d = kmalloc(bs), *d;

	inode_t *ext2_vfs = kmalloc(sizeof(inode_t));
	ext2_vfs->name = strdup("hd");
	ext2_vfs->type = FS_DIR;
	ext2_vfs->list = kmalloc(sizeof(dentry_t));
	ext2_vfs->list->count = 0;
	ext2_vfs->list->head = NULL;
	inode_t *_tmp = NULL;

	for(k = 0; k < count; ++k)
	{
		d = (ext2_dentry_t*)(get_block(dev_inode, sb, *(ptrs + k), _d));
		if(d)
		{
			uint32_t size = 0;
			while(size <= bs)
			{
				if((!d->size) || (size + d->size > bs)) break;
				//if(d->name_length)
				{
					//uint8_t *s = strndup(d->name, d->name_length);
					//debug("%s\n", s);
					//kfree(s);
				}

				if(!_tmp)
					ext2_vfs->list->head = _tmp = kmalloc(sizeof(inode_t));
				else
					_tmp = _tmp->next = kmalloc(sizeof(inode_t));

				_tmp->next = NULL;
				_tmp->name = strndup(d->name, d->name_length);
				debug("%s\n", _tmp->name);
				ext2_inode_t *_inode = ext2_get_inode(dev_inode, sb, d->inode);
				_tmp->size = _inode->size;
				kfree(_inode);
				_tmp->type = FS_FILE;
				_tmp->dev  = dev_inode->dev;
				_tmp->fs   = &ext2fs;
				_tmp->p = kmalloc(sizeof(ext2_private_t));
				*(ext2_private_t*)_tmp->p = (ext2_private_t)
						{
							.sb = sb,
							.inode = d->inode,
						};
				ext2_vfs->list->count++;

				size += d->size;
				d = (ext2_dentry_t*)((uint64_t)d + d->size);
			}
			//kfree(d);
		}
	}

	kfree(_d);

	return ext2_vfs;
#endif
}

static size_t ext2_inode_get_block(struct fs_node *node, struct ext2_inode *inode, size_t idx, void *buf)
{
    printk("ext2_inode_get_block(node=%p, inode=%p, idx=%d, buf=%p)\n", node, inode, idx, buf);
    ext2_private_t *priv = node->p;
    size_t bs = 1024UL << priv->superblock->block_size;
    size_t blocks_nr = inode->size / bs;
    size_t ptrs_per_block = inode->size / bs;

    if (idx >= blocks_nr) {
        panic("Trying to get invalid block!\n");
    }

    if (idx < EXT2_DIRECT_POINTERS) {
        read_block(priv->supernode, priv->superblock, inode->direct_pointer[idx], buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

static ssize_t ext2_read(struct fs_node *node, off_t offset, size_t _size, void *_buf)
{
	printk("Reading file %s\n", node->name);
	ext2_private_t *p = node->p;

	uint32_t bs = 1024UL << p->superblock->block_size;
	struct ext2_inode *inode = ext2_get_inode(node, p->superblock, p->inode);
    uint64_t size = inode->size | ((uint64_t) inode->size_high << 32);

    char *buf = _buf;

    /* Read direct pointers */
    size_t i = 0;
    while (i < 12 && size >= bs) {
        read_block(node, p->superblock, inode->direct_pointer[i++], buf);
        buf += bs;
        size -= bs;
    }
    /* Read singly indirect pointers */
    /* Read doubly indirect pointers */
    /* Read triply indirect pointers */

	//uint32_t *ptrs = get_block_ptrs(inode, p->sb, i);
	//uint32_t count = *ptrs++;

	//uint32_t skip_blocks = offset/bs; // Blocks to be skipped
	//uint32_t skip_bytes  = offset%bs; // Bytes  to be skipped

	//if(count < skip_blocks) return 0;

	//count -= skip_blocks;
	//ptrs  += skip_blocks;

	//// Now let's read the first part ( size <= bs )
	//uint32_t first_part_size = (count > 1)?MIN(bs - skip_bytes, size):MIN(size, inode->size);
	//inode->dev->read(inode, (*ptrs++) * bs + skip_bytes, first_part_size, buf);
	//buf += first_part_size;
	//size -= first_part_size;
	//--count;

	//// Now let's read the second part ( size is a multible of bs )
	//while(count > 1 && size > bs)
	//{
	//	inode->dev->read(inode, *ptrs++ * bs, bs, buf);
	//	buf += bs;
	//	size -= bs;
	//	--count;
	//}

	//// Now let's read the last part ( size <= bs)
	//if(count)
	//{
	//	uint32_t last_part_size = MIN(size, inode->size - ( _size - size ));
	//	inode->dev->read(inode, (*ptrs++) * bs, last_part_size, buf);
	//	buf += last_part_size;
	//	size -= last_part_size;
	//	--count;
	//}

	return _size - size;
}

static char readdir_buf[4096] __aligned(16);
static ssize_t ext2_readdir(struct fs_node *dir, off_t offset, struct dirent *dirent)
{
    printk("ext2_readdir(dir=%p, offset=%d, dirent=%p)\n", dir, offset, dirent);

    if (dir->type != FS_DIR)
        return -ENOTDIR;

    ext2_private_t *p = dir->p;
    struct ext2_inode *inode = ext2_get_inode(p->supernode, p->superblock, p->inode);

    if (inode->type != EXT2_INODE_TYPE_DIR) {
        kfree(inode);
        return -ENOTDIR;
    }

	size_t bs = 1024UL << p->superblock->block_size;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs);    //readdir_buf;
    struct ext2_dentry *d;
    off_t idx = 0;
    char found = 0;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_get_block(dir, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            if (idx == offset) {
                dirent->d_ino = d->inode;
                strcpy(dirent->d_name, (char *) d->name);  /* FIXME */
                found = 1;
                break;
            }

            d = (struct ext2_dentry *) ((char *) d + d->size);
            ++idx;
        }
    }

    kfree(buf);
    kfree(inode);

    return found;
}

#if 0
static uint32_t ext2_mount(inode_t *dst, inode_t *src)
{
	inode_t *i = ext2_load(src);

	if(!i) return -1;
	vfs_mount(dst, "/", i);

	return 1;
}
#endif

struct fs ext2fs = {
    .name = "ext2",
    .load = ext2_load,
    .read = ext2_read,
    .readdir = ext2_readdir,

    .f_ops = {
        .open = generic_file_open,
        .read = generic_file_read,
        .readdir = generic_file_readdir,
    }
};
