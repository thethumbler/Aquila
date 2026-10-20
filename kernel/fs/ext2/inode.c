#include <ext2.h>
#include <ds/bitmap.h>
#include <core/panic.h>

static inline off_t ext2_inode_off(struct ext2 *desc, ino_t ino)
{
    /* invalid inode */
    if (!ino || ino > desc->superblock->ninodes)
        return -EINVAL;

    uint32_t group_id = (ino - 1) / desc->superblock->ginodes;
    uint32_t index    = (ino - 1) % desc->superblock->ginodes;

    struct ext2_group *group = &desc->groups[group_id];
    return group->inodes * desc->bs + index * desc->superblock->inode_size;
}

int ext2_inode_read(struct ext2 *desc, ino_t ino, struct ext2_inode *ref)
{
    off_t off;
    
    if ((off = ext2_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
        size_t size = sizeof(struct ext2_inode);
        ssize_t r;

        if ((r = vfs_read(desc->supernode, off, size, ref)) != (ssize_t) size) {
            if (r < 0)
                return r;

            /* FIXME Check for further errors? */
            return -EINVAL;
        }
    }

    return 0;
}

int ext2_inode_write(struct ext2 *desc, ino_t ino, struct ext2_inode *ext2_inode)
{
    off_t off = ext2_inode_off(desc, ino);
    size_t inode_size = desc->superblock->inode_size;

    if (off < 0)
        return off;
     
    return vfs_write(desc->supernode, off, sizeof(struct ext2_inode), ext2_inode);
}

size_t ext2_inode_block_read(struct ext2 *desc, struct ext2_inode *inode, size_t idx, void *buf)
{
    size_t bs = desc->bs;
    size_t blocks_nr = (inode->size + bs - 1) / bs;
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx >= blocks_nr) {
        panic("Trying to get invalid block!\n");
    }

    if (idx < EXT2_DIRECT_POINTERS) {
        if (!inode->direct_pointer[idx]) {  /* sparse block */
            memset(buf, 0, bs);
            return 0;
        }

        ext2_block_read(desc, inode->direct_pointer[idx], buf);
        return 0;
    }

    idx -= EXT2_DIRECT_POINTERS;
    
    if (idx < p) {
        /* singly indirect */
        if (!inode->singly_indirect_pointer) {  /* sparse block */
            memset(buf, 0, bs);
            return 0;
        }

        uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx];
        kfree(tmp);

        if (!block) {   /* sparse block */
            memset(buf, 0, bs);
            return 0;
        }

        ext2_block_read(desc, block, buf);
        return 0;
    }

    idx -= p;
    
    if (idx < p * p) {
        /* doubly indirect */
        if (!inode->doubly_indirect_pointer) {  /* sparse block */
            memset(buf, 0, bs);
            return 0;
        }

        uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        ext2_block_read(desc, inode->doubly_indirect_pointer, tmp);
        uint32_t block = tmp[idx / p];

        if (block) {
            ext2_block_read(desc, block, tmp);
            block = tmp[idx % p];
        }

        kfree(tmp);

        if (!block) {   /* sparse block */
            memset(buf, 0, bs);
            return 0;
        }

        ext2_block_read(desc, block, buf);
        return 0;
    }

    panic("Not impelemented");

    /* unreachable */
}

/* allocate a block owned by the inode, i_blocks counts 512-byte sectors */
static int ext2_inode_block_alloc(struct ext2 *desc, struct ext2_inode *inode, uint32_t *block)
{
    if (!(*block = ext2_block_alloc(desc)))
        return -ENOSPC;

    inode->blocks += desc->bs / 512;
    return 0;
}

/* get the block at idx of an indirect block, allocating it if needed */
static int ext2_indirect_block_get(struct ext2 *desc, struct ext2_inode *inode, uint32_t table, size_t idx, uint32_t *block)
{
    int err = 0;
    uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);

    if (!tmp)
        return -ENOMEM;

    ext2_block_read(desc, table, tmp);

    if (!(*block = tmp[idx])) {    /* Allocate */
        if (!(err = ext2_inode_block_alloc(desc, inode, block))) {
            tmp[idx] = *block;
            ext2_block_write(desc, table, tmp);
        }
    }

    kfree(tmp);
    return err;
}

ssize_t ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf)
{
    size_t p = desc->bs / 4;    /* Pointers per block */
    uint32_t nblocks = inode->blocks;
    uint32_t block = 0;
    int err = 0;

    if (idx < EXT2_DIRECT_POINTERS) {
        if (!(block = inode->direct_pointer[idx])) {    /* Allocate */
            if (!(err = ext2_inode_block_alloc(desc, inode, &block)))
                inode->direct_pointer[idx] = block;
        }
    } else if ((idx -= EXT2_DIRECT_POINTERS) < p) {
        uint32_t table = inode->singly_indirect_pointer;

        if (!table && !(err = ext2_inode_block_alloc(desc, inode, &table)))
            inode->singly_indirect_pointer = table;

        if (!err)
            err = ext2_indirect_block_get(desc, inode, table, idx, &block);
    } else if ((idx -= p) < p * p) {
        uint32_t table = inode->doubly_indirect_pointer;
        uint32_t indirect = 0;

        if (!table && !(err = ext2_inode_block_alloc(desc, inode, &table)))
            inode->doubly_indirect_pointer = table;

        if (!err)
            err = ext2_indirect_block_get(desc, inode, table, idx / p, &indirect);

        if (!err)
            err = ext2_indirect_block_get(desc, inode, indirect, idx % p, &block);
    } else {
        /* TODO triply indirect blocks */
        return -EFBIG;
    }

    /* persist new block pointers and i_blocks */
    if (inode->blocks != nblocks)
        ext2_inode_write(desc, inode_nr, inode);

    if (err)
        return err;

    ext2_block_write(desc, block, buf);
    return 0;
}

ino_t ext2_inode_alloc(struct ext2 *desc)
{
    vfs_log(LOG_DEBUG, "ext2_inode_alloc(desc=%p)\n", desc);

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    uint32_t inode = 0, real_inode = 0, group = 0;
    struct bitmap bm = {0};
    int found = 0;

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        inode = 0;

        if (desc->groups[i].free_inodes) {
            ext2_block_read(desc, desc->groups[i].imap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->ginodes;
            
            /* look for a free inode */
            for (; inode < bm.max_idx && bitmap_check(&bm, inode); ++inode);

            if (inode == bm.max_idx)
                continue;

            group = i;
            real_inode = inode + group * desc->superblock->ginodes + 1;
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Out of space */
        kfree(buf);
        return 0;
    }

    //printk("group %d, inode %d, real_inode %d\n", group, inode, real_inode);

    /* Update bitmap */
    bitmap_set(&bm, inode);
    ext2_block_write(desc, desc->groups[group].imap, buf);

    /* Update block group descriptor */
    desc->groups[group].free_inodes--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->free_inodes--;
    ext2_superblock_rewrite(desc);

    kfree(buf);
    return real_inode;
}
