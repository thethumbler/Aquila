#include <core/panic.h>
#include <fs/vfs.h>
#include <fs/ext2.h>
#include <bits/errno.h>

/*
 * Ext 2 Helpers
 */

typedef size_t ext2_inode_t;

typedef struct {
    struct fs_node *supernode;
	struct ext2_superblock *superblock;
	ext2_inode_t inode;
} ext2_private_t;

static struct ext2_inode *ext2_get_inode(struct fs_node *node, struct ext2_superblock *sb, ext2_inode_t inode)
{
    printk("ext2_get_inode(node=%p, sb=%p, inode=%d)\n", node, sb, inode);
	uint32_t bs = 1024UL << sb->block_size;
    uint32_t bgd_table = bs == 1024? 2*1024 : bs;
	uint32_t block_group = (inode - 1) / sb->inodes_per_block_group;
	struct ext2_block_group_descriptor *bgd = kmalloc(sizeof(struct ext2_block_group_descriptor));

	vfs.read(node, bgd_table + block_group, sizeof(struct ext2_block_group_descriptor), bgd);

	uint32_t index = (inode - 1) % sb->inodes_per_block_group;
	struct ext2_inode *i = kmalloc(sizeof(struct ext2_inode));

	vfs.read(node, bgd->inode_table * bs + index * sb->inode_size, sizeof(struct ext2_inode), i);
    kfree(bgd);
	return i;
}

static void *read_block(struct fs_node *supernode, struct ext2_superblock *sb, uint32_t number, void *buf)
{
    printk("read_block(supernode=%p, sb=%p, number=%d, buf=%p)\n", supernode, sb, number, buf);
	uint32_t bs = 1024UL << sb->block_size;
	vfs.read(supernode, number * bs, bs, buf);
	return buf;
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

/*
 * VFS Routines
 */

static struct fs_node *ext2_load(struct fs_node *node)
{
    printk("ext2_load(node=%p [name=%s])\n", node, node->name);

    /* Read Superblock */
	struct ext2_superblock *sb = kmalloc(sizeof(*sb));
	vfs.read(node, 1024, sizeof(*sb), sb);

    /* Valid Ext2? */
	if (sb->ext2_signature != EXT2_SIGNATURE) {
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
}

static struct fs_node *ext2_traverse(struct vfs_path *path)
{
    printk("ext2_traverse(path=%p)\n", path);

    for (;;);

    return NULL;
}

static ssize_t ext2_read(struct fs_node *node, off_t offset, size_t size, void *buf)
{
	printk("Reading file %s\n", node->name);
	ext2_private_t *p = node->p;

	size_t bs = 1024UL << p->superblock->block_size;
	struct ext2_inode *inode = ext2_get_inode(node, p->superblock, p->inode);
    size_t blocks_nr = inode->size / bs;    /* XXX */

    /* XXX? */
    if ((offset + size) / bs >= blocks_nr) {
        return 0;
    }

    char *read_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs);
            read_block(p->supernode, p->superblock, offset/bs, read_buf);
            memcpy(_buf, read_buf + (offset % bs), start);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto free_resources;
        }
    }

    /* Read whole blocks */
    size_t count = size/bs;

    while (count) {
        read_block(p->supernode, p->superblock, offset/bs, _buf);

        ret    += bs;
        size   -= bs;
        _buf   += bs;
        offset += bs;
        --count;
    }

    if (!size)
        goto free_resources;

    size_t end = size % bs;

    if (end) {
        if (!read_buf)
            read_buf = kmalloc(bs);

        read_block(p->supernode, p->superblock, offset/bs, read_buf);
        memcpy(_buf, read_buf, end);
        ret += end;
    }

free_resources:
    if (read_buf)
        kfree(read_buf);
    return ret;
}

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

    char *buf = kmalloc(bs);
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
    //.find = ext2_find,
    .traverse = ext2_traverse,

    .f_ops = {
        .open = generic_file_open,
        .read = generic_file_read,
        .readdir = generic_file_readdir,
    }
};
