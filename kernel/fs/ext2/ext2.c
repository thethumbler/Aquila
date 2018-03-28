#include <core/panic.h>
#include <fs/vfs.h>
#include <fs/ext2.h>
#include <fs/posix.h>
#include <fs/itbl.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

/*
 * Ext 2 Helpers
 */

/* Ext 2 descriptor structure */
typedef struct {
    struct inode *supernode;
    struct ext2_superblock *superblock;
    struct ext2_block_group_descriptor *bgd_table;
    size_t bgds_count;
    size_t bs;
    struct itbl *itbl;
} ext2_desc_t;

/* ================== Super Block helpers ================== */

static void ext2_superblock_rewrite(ext2_desc_t *desc)
{
    vfs_write(desc->supernode, 1024, sizeof(struct ext2_superblock), desc->superblock);
}

/* ================== Block Group Descriptors helpers ================== */

static void ext2_bgd_table_rewrite(ext2_desc_t *desc)
{
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_size = desc->bgds_count * sizeof(struct ext2_block_group_descriptor);
    vfs_write(desc->supernode, bgd_table, bgds_size, desc->bgd_table);
}

/* ================== Block helpers ================== */

static void *ext2_block_read(ext2_desc_t *desc, uint32_t number, void *buf)
{
    //printk("ext2_block_read(desc=%p, number=%d, buf=%p)\n", desc, number, buf);
    vfs_read(desc->supernode, number * desc->bs, desc->bs, buf);
    return buf;
}

static void *ext2_block_write(ext2_desc_t *desc, uint32_t number, void *buf)
{
    //printk("ext2_block_write(desc=%p, number=%d, buf=%p)\n", desc, number, buf);
    vfs_write(desc->supernode, number * desc->bs, desc->bs, buf);
    return buf;
}

static uint32_t ext2_block_allocate(ext2_desc_t *desc)
{
    //printk("ext2_block_allocate(desc=%p)\n", desc);
    uint32_t *buf = kmalloc(desc->bs);
    uint32_t block = 0, real_block = 0, group = 0;
    bitmap_t bm = {0};

    for (unsigned i = 0; i < desc->bgds_count; ++i) {
        if (desc->bgd_table[i].unallocated_blocks_count) {
            ext2_block_read(desc, desc->bgd_table[i].block_usage_bitmap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->blocks_per_block_group;
            
            /* Look for a free block */
            for (; bitmap_check(&bm, block); ++block);
            group = i;
            real_block = block + group * desc->superblock->blocks_per_block_group;
            break;
        }
    }

    if (!block) {
        /* Out of space */
        kfree(buf);
        return 0;
    }

    //printk("group %d, block %d, real_block %d\n", group, block, real_block);

    /* Update bitmap */
    bitmap_set(&bm, block);
    ext2_block_write(desc, desc->bgd_table[group].block_usage_bitmap, buf);

    /* Update block group descriptor */
    desc->bgd_table[group].unallocated_blocks_count--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->unallocated_blocks_count--;
    ext2_superblock_rewrite(desc);

    /* Clear block */
    memset(buf, 0, desc->bs);
    ext2_block_write(desc, real_block, buf);


    kfree(buf);
    return real_block;
}

/* ================== Inode helpers ================== */

static struct ext2_inode *ext2_inode_read(ext2_desc_t *desc, uint32_t inode)
{
    if (inode >= desc->superblock->inodes_count)    /* Invalid inode */
        return NULL;

    uint32_t block_group = (inode - 1) / desc->superblock->inodes_per_block_group;
    struct ext2_block_group_descriptor *bgd = &desc->bgd_table[block_group];

    uint32_t index = (inode - 1) % desc->superblock->inodes_per_block_group;
    
    struct ext2_inode *i = kmalloc(sizeof(struct ext2_inode));
    vfs_read(desc->supernode, bgd->inode_table * desc->bs + index * desc->superblock->inode_size, sizeof(*i), i);
    return i;
}

static struct ext2_inode *ext2_inode_write(ext2_desc_t *desc, uint32_t inode, struct ext2_inode *i)
{
    //printk("ext2_inode_write(desc=%p, inode=%d, i=%p)\n", desc, inode, i);
    if (inode >= desc->superblock->inodes_count)    /* Invalid inode */
        return NULL;

    uint32_t block_group = (inode - 1) / desc->superblock->inodes_per_block_group;
    struct ext2_block_group_descriptor *bgd = &desc->bgd_table[block_group];

    uint32_t index = (inode - 1) % desc->superblock->inodes_per_block_group;
    uint32_t inode_size = desc->superblock->inode_size;
    
    vfs_write(desc->supernode, bgd->inode_table * desc->bs + index * inode_size, inode_size, i);
    return i;
}

static size_t ext2_inode_read_block(ext2_desc_t *desc, struct ext2_inode *inode, size_t idx, void *buf)
{
    //printk("ext2_inode_read_block(desc=%p, inode=%p, idx=%d, buf=%p)\n", desc, inode, idx, buf);
    size_t bs = desc->bs;
    size_t blocks_nr = (inode->size + bs - 1) / bs;
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx >= blocks_nr) {
        panic("Trying to get invalid block!\n");
    }

    if (idx < EXT2_DIRECT_POINTERS) {
        ext2_block_read(desc, inode->direct_pointer[idx], buf);
    } else if (idx < EXT2_DIRECT_POINTERS + p) {
        uint32_t *tmp = kmalloc(desc->bs);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx - EXT2_DIRECT_POINTERS];
        kfree(tmp);
        ext2_block_read(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

static size_t ext2_inode_write_block(ext2_desc_t *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf)
{
    //printk("ext2_inode_write_block(desc=%p, inode=%p, inode_nr=%d, idx=%d, buf=%p)\n", desc, inode, inode_nr, idx, buf);
    size_t bs = desc->bs;
    size_t blocks_nr = (inode->size + bs - 1) / bs;
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx < EXT2_DIRECT_POINTERS) {
        if (!inode->direct_pointer[idx]) {    /* Allocate */
            inode->direct_pointer[idx] = ext2_block_allocate(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }
        ext2_block_write(desc, inode->direct_pointer[idx], buf);
    } else if (idx < EXT2_DIRECT_POINTERS + p) {
        if (!inode->singly_indirect_pointer) {    /* Allocate */
            inode->singly_indirect_pointer = ext2_block_allocate(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }

        uint32_t *tmp = kmalloc(desc->bs);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx - EXT2_DIRECT_POINTERS];

        if (!block) {   /* Allocate */
            tmp[idx - EXT2_DIRECT_POINTERS] = ext2_block_allocate(desc);
            ext2_block_write(desc, inode->singly_indirect_pointer, tmp);
        }

        kfree(tmp);
        ext2_block_write(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

static uint32_t ext2_inode_allocate(ext2_desc_t *desc)
{
    //printk("ext2_inode_allocate(desc=%p)\n", desc);
    uint32_t *buf = kmalloc(desc->bs);
    uint32_t inode = 0, real_inode = 0, group = 0;
    bitmap_t bm = {0};

    for (unsigned i = 0; i < desc->bgds_count; ++i) {
        if (desc->bgd_table[i].unallocated_inodes_count) {
            ext2_block_read(desc, desc->bgd_table[i].inode_usage_bitmap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->inodes_per_block_group;
            
            /* Look for a free inode */
            for (; bitmap_check(&bm, inode); ++inode);
            group = i;
            real_inode = inode + group * desc->superblock->inodes_per_block_group;
            break;
        }
    }

    if (!inode) {
        /* Out of space */
        kfree(buf);
        return 0;
    }

    //printk("group %d, inode %d, real_inode %d\n", group, inode, real_inode);

    /* Update bitmap */
    bitmap_set(&bm, inode);
    ext2_block_write(desc, desc->bgd_table[group].inode_usage_bitmap, buf);

    /* Update block group descriptor */
    desc->bgd_table[group].unallocated_inodes_count--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->unallocated_inodes_count--;
    ext2_superblock_rewrite(desc);

    kfree(buf);
    return real_inode;
}

static int ext2_inode_build(ext2_desc_t *desc, size_t inode, struct inode **ref_inode)
{
    printk("ext2_inode_build(desc=%p, inode=%d, ref_inode=%p)\n", desc, inode, ref_inode);

    struct inode *node = NULL;

    /* In open inode table? */
    if ((node = itbl_find(desc->itbl, inode)))
        goto found;

    node = kmalloc(sizeof(struct inode));
    memset(node, 0, sizeof(*node));

    struct ext2_inode *i = ext2_inode_read(desc, inode);

    switch (i->type) {
        case EXT2_INODE_TYPE_FIFO:  node->type = FS_FIFO; break;
        case EXT2_INODE_TYPE_CHR:   node->type = FS_CHRDEV; break;
        case EXT2_INODE_TYPE_DIR:   node->type = FS_DIR; break;
        case EXT2_INODE_TYPE_BLK:   node->type = FS_BLKDEV; break;
        case EXT2_INODE_TYPE_RGL:   node->type = FS_RGL; break;
        case EXT2_INODE_TYPE_SLINK: node->type = FS_SYMLINK; break;
        case EXT2_INODE_TYPE_SCKT:  node->type = FS_SOCKET; break;
    }

    node->id   = inode;
    node->size = i->size;
    node->mask = i->permissions;
    node->uid  = i->uid;
    node->gid  = i->gid;

    node->fs   = &ext2fs;
    kfree(i);
    node->p = desc;

    itbl_insert(desc->itbl, node);

found:
    if (ref_inode)
        *ref_inode = node;

    return 0;
}

/* ================== dentry helpers ================== */

static uint32_t ext2_dentry_find(ext2_desc_t *desc, struct ext2_inode *inode, const char *name)
{
    //printk("ext2_dentry_find(desc=%p, inode=%p, name=%s)\n", desc, inode, name);

    if (inode->type != EXT2_INODE_TYPE_DIR)
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs);
    struct ext2_dentry *d;
    uint32_t inode_nr;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_read_block(desc, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            char _name[d->name_length+1];
            memcpy(_name, d->name, d->name_length);
            _name[d->name_length] = 0;
            if (!strcmp((char *) _name, name))
                goto found;
            d = (struct ext2_dentry *) ((char *) d + d->size);
        }
    }
    
    /* Not found */
    kfree(buf);
    return 0;

found:
    inode_nr = d->inode;
    kfree(buf);
    return inode_nr;
}

static int ext2_dentry_create(struct vnode *dir, const char *name, uint32_t inode, uint8_t type)
{
    //printk("ext2_dentry_create(dir=%p, name=%s, inode=%d, type=%d)\n", dir, name, inode, type);

    if (dir->type != FS_DIR)    /* Not a directory */
        return -ENOTDIR;

    ext2_desc_t *desc = dir->super->p;

    size_t name_length = strlen(name);
    size_t size = name_length + sizeof(struct ext2_dentry);
    size = (size + 3) & ~3; /* Align to 4 bytes */

    /* Look for a candidate entry */
    char *buf = kmalloc(desc->bs);
    struct ext2_dentry *last = NULL;
    struct ext2_dentry *cur = NULL;
    struct ext2_inode *dir_inode = ext2_inode_read(desc, dir->id);
    size_t bs = desc->bs;
    size_t blocks_nr = dir_inode->size / bs;
    size_t flag = 0;    /* 0 => allocate, 1 => replace, 2 => split */
    size_t block = 0;

    for (block = 0; block < blocks_nr; ++block) {
        ext2_inode_read_block(desc, dir_inode, block, buf);
        cur = (struct ext2_dentry *) buf;

        while ((char *) cur < (char *) buf + bs) {
            if ((!cur->inode) && cur->size >= size) {   /* unused entry */
                flag = 1;   /* Replace */
                goto done;
            }

            if ((((cur->name_length + sizeof(struct ext2_dentry) + 3) & ~3) + size) <= cur->size) {
                flag = 2;   /* Split */
                goto done;
            }

            cur = (struct ext2_dentry *) ((char *) cur + cur->size);
        }
    }

done:
    if (flag == 1) {    /* Replace */
        memcpy(cur->name, name, name_length);
        cur->inode = inode;
        cur->name_length = name_length;
        cur->type = type;
        /* Update block */
        ext2_inode_write_block(desc, dir_inode, dir->id, block, buf);
    } else if (flag == 2) { /* Split */
        size_t new_size = (cur->name_length + sizeof(struct ext2_dentry) + 3) & ~3;
        struct ext2_dentry *next = (struct ext2_dentry *) ((char *) cur + new_size);

        next->inode = inode;
        next->size = cur->size - new_size;
        next->name_length = name_length;
        next->type = type;
        memcpy(next->name, name, name_length);

        cur->size = new_size;

        /* Update block */
        ext2_inode_write_block(desc, dir_inode, dir->id, block, buf);
    } else {    /* Allocate */
        panic("Not impelemented\n");
    }

    kfree(dir_inode);
    kfree(buf);
    return 0;
}

/* ================== VFS routines ================== */

static int ext2_init()
{
    //printk("ext2_init()\n");
    return 0;
}

static int ext2_load(struct inode *dev, struct inode **super)
{
    /* Read Superblock */
    struct ext2_superblock *sb = kmalloc(sizeof(*sb));
    vfs_read(dev, 1024, sizeof(*sb), sb);

    /* Valid Ext2? */
    if (sb->ext2_signature != EXT2_SIGNATURE) {
        kfree(sb);
        return -EINVAL;
    }

    /* Build descriptor structure */
    ext2_desc_t *desc = kmalloc(sizeof(ext2_desc_t));
    desc->supernode = dev;
    desc->superblock = sb;
    desc->bs = 1024UL << sb->block_size;

    /* Read block group descriptor table */
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_count = sb->blocks_count/sb->blocks_per_block_group;
    size_t bgds_size  = bgds_count * sizeof(struct ext2_block_group_descriptor);
    desc->bgds_count = bgds_count;
    desc->bgd_table = kmalloc(bgds_size);
    vfs_read(desc->supernode, bgd_table, bgds_size, desc->bgd_table);

    desc->itbl = kmalloc(sizeof(struct itbl));
    itbl_init(desc->itbl);

    if (super)
        ext2_inode_build(desc, 2, super);
    
    return 0;
}

static int ext2_mount(const char *dir, int flags, void *data)
{
    printk("ext2_mount(dir=%s, flags=%x, data=%p)\n", dir, flags, data);

    struct {
        char *dev;
        char *opt;
    } *sdata = data;

    //printk("data{dev=%s, opt=%s}\n", sdata->dev, sdata->opt);

    struct vnode vnode;
    struct inode *dev;
    int ret;

    struct uio uio = {0};   /* FIXME */
    if ((ret = vfs_lookup(sdata->dev, &uio, &vnode, NULL))) {
        panic("Could not load device");
    }

    vfs_vget(&vnode, &dev);

    struct inode *fs;
    ext2fs.load(dev, &fs);
    vfs_bind(dir, fs);

    return 0;
}

static int ext2_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    printk("ext2_vfind(dir=%p, fn=%s, child=%p)\n", dir, fn, child);

    ext2_desc_t *desc = dir->super->p;

    uint32_t inode = dir->id;
    size_t inode_size = 0;

    struct ext2_inode *i = ext2_inode_read(desc, inode);
    uint32_t inode_nr = ext2_dentry_find(desc, i, fn);
    kfree(i);

    if (!inode_nr)  /* Not found */
        return -ENOENT;

    inode = inode_nr;

    if (child) {
        child->id = inode_nr;
    }

    return 0;
}

static int ext2_vget(struct vnode *vnode, struct inode **inode)
{
    printk("ext2_vget(vnode=%p, inode=%p)\n", vnode, inode);

    ext2_desc_t *desc = vnode->super->p;

    if (inode) {
        ext2_inode_build(desc, vnode->id, inode);
    }

    return 0;
}

static ssize_t ext2_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    //printk("ext2_read(node=%p, offset=%d, size=%d, buf=%p)\n", node, offset, size, buf);

    ext2_desc_t *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode *inode = ext2_inode_read(desc, node->id);

    if ((size_t) offset >= inode->size) {
        return 0;
    }

    size = MIN(size, inode->size - offset);
    
    char *read_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs);
            ext2_inode_read_block(desc, inode, offset/bs, read_buf);
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
        ext2_inode_read_block(desc, inode, offset/bs, _buf);

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

        ext2_inode_read_block(desc, inode, offset/bs, read_buf);
        memcpy(_buf, read_buf, end);
        ret += end;
    }

free_resources:
    kfree(inode);
    if (read_buf)
        kfree(read_buf);
    return ret;
}

static ssize_t ext2_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    printk("ext2_write(node=%p, offset=%d, size=%d, buf=%p)\n", node, offset, size, buf);

    ext2_desc_t *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode *inode = ext2_inode_read(desc, node->id);

    if ((size_t) offset + size > inode->size) {
        inode->size = offset + size;
        ext2_inode_write(desc, node->id, inode);
    }

    size = MIN(size, inode->size - offset);
    
    char *write_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs);
            ext2_inode_read_block(desc, inode, offset/bs, write_buf);
            memcpy(write_buf + (offset % bs), _buf, start);
            ext2_inode_write_block(desc, inode, node->id, offset/bs, write_buf);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto free_resources;
        }
    }

    /* Write whole blocks */
    size_t count = size/bs;

    while (count) {
        ext2_inode_write_block(desc, inode, node->id, offset/bs, _buf);

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
        if (!write_buf)
            write_buf = kmalloc(bs);

        ext2_inode_read_block(desc, inode, offset/bs, write_buf);
        memcpy(write_buf, _buf, end);
        ext2_inode_write_block(desc, inode, node->id, offset/bs, write_buf);
        ret += end;
    }

free_resources:
    kfree(inode);
    if (write_buf)
        kfree(write_buf);
    return ret;
}

static int ext2_create(struct vnode *dir, const char *fn, struct uio *uio, struct inode **node)
{
    printk("ext2_create(dir=%p, fn=%s, uio=%p, node=%p)\n", dir, fn, uio, node);

    if (!fn || !*fn)
        return -EINVAL;

    ext2_desc_t *desc = dir->super->p;
    struct ext2_inode *dir_inode = ext2_inode_read(desc, dir->id);

    if (!dir_inode) /* Invalid inode? */
        return -ENOTDIR;

    if (ext2_dentry_find(desc, dir_inode, fn)) {
        /* File exists */
        kfree(dir_inode);
        return -EEXIST;
    }

    uint32_t inode_nr = ext2_inode_allocate(desc);
    struct ext2_inode *inode = ext2_inode_read(desc, inode_nr);

    memset(inode, 0, sizeof(*inode));

    inode->hard_links_count = 1;
    inode->type = EXT2_INODE_TYPE_RGL;

    ext2_inode_write(desc, inode_nr, inode);
    ext2_dentry_create(dir, fn, inode_nr, EXT2_DENTRY_TYPE_RGL);

    kfree(dir_inode);
    kfree(inode);

    if (node)
        ext2_inode_build(desc, inode_nr, node);

    return 0;
}

static int ext2_mkdir(struct inode *dir, const char *name)
{
#if 0
    printk("ext2_mkdir(dir=%p, name=%s)\n", dir, name);

    if (!name)
        return -EINVAL;

    ext2_desc_t *desc = dir->p;
    struct ext2_inode *dir_inode = ext2_inode_read(desc, dir->id);

    if (!dir_inode) /* Invalid inode? */
        return -EINVAL;

    if (ext2_dentry_find(desc, dir_inode, name)) {
        /* Directory exists */
        kfree(dir_inode);
        return -EEXIST;
    }

    uint32_t inode_nr = ext2_inode_allocate(desc);
    struct ext2_inode *inode = ext2_inode_read(desc, inode_nr);

    memset(inode, 0, sizeof(*inode));

    inode->hard_links_count = 2;
    inode->type = EXT2_INODE_TYPE_DIR;
    inode->size = desc->bs;
    
    char *buf = kmalloc(desc->bs);
    struct ext2_dentry *d = (struct ext2_dentry *) buf;
    d->inode = inode_nr;
    d->size = 12;
    d->name_length = 1;
    d->type = EXT2_DENTRY_TYPE_DIR;
    memcpy(d->name, ".", 1);
    d = (struct ext2_dentry *) ((char *) d + 12);
    d->inode = p->inode;
    d->size = desc->bs - 12;
    d->name_length = 2;
    d->type = EXT2_DENTRY_TYPE_DIR;
    memcpy(d->name, "..", 2);

    /* Will rewrite inode */
    ext2_inode_write_block(desc, inode, inode_nr, 0, buf);
    kfree(buf);

    //ext2_inode_write(p->desc, inode_nr, inode);
    ext2_dentry_create(dir, name, inode_nr, EXT2_DENTRY_TYPE_DIR);

    kfree(dir_inode);
    kfree(inode);
    //return ext2_inode_to_fs_node(p->desc, inode_nr);
#endif
    return 0;
}

static ssize_t ext2_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    printk("ext2_readdir(dir=%p, offset=%d, dirent=%p)\n", dir, offset, dirent);

    if (dir->type != FS_DIR)
        return -ENOTDIR;

    ext2_desc_t *desc = dir->p;
    struct ext2_inode *inode = ext2_inode_read(desc, dir->id);

    if (inode->type != EXT2_INODE_TYPE_DIR) {
        kfree(inode);
        return -ENOTDIR;
    }

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs);
    struct ext2_dentry *d;
    off_t idx = 0;
    char found = 0;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_read_block(desc, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            if (idx == offset) {
                dirent->d_ino = d->inode;
                memcpy(dirent->d_name, (char *) d->name, d->name_length);
                dirent->d_name[d->name_length] = '\0';
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

static int ext2_eof(struct file *file)
{
    //printk("ext2_eof(file=%p)\n", file);
    return (size_t) file->offset >= file->node->size;
}

struct fs ext2fs = {
    .name  = "ext2",
    .init  = ext2_init,
    .load  = ext2_load,
    .mount = ext2_mount,

    .iops = {
        .read    = ext2_read,
        .write   = ext2_write,
        .create  = ext2_create,
        .readdir = ext2_readdir,
        //.mkdir   = ext2_mkdir,
        .vfind   = ext2_vfind,
        .vget    = ext2_vget,
    },

    .fops = {
        .open    = generic_file_open,
        .read    = posix_file_read,
        .write   = posix_file_write,
        .readdir = posix_file_readdir,

        .eof     = ext2_eof,
    }
};
