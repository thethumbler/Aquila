#include <ext2.h>
#include <ds/bitmap.h>

static inline off_t ext2_inode_off(struct ext2 *desc, ino_t ino)
{
    /* invalid inode */
    if (!ino || ino > desc->superblock->ninodes)
        return -EINVAL;

    uint32_t group_id = (ino - 1) / desc->superblock->ginodes;
    uint32_t index    = (ino - 1) % desc->superblock->ginodes;

    struct ext2_group *group = &desc->groups[group_id];
    return group->inodes * desc->bs + index * desc->inode_size;
}

int ext2_inode_read(struct ext2 *desc, ino_t ino, struct ext2_inode *ref)
{
    off_t off;

    if ((off = ext2_inode_off(desc, ino)) < 0)
        return off;

    return ref ? ext2_io(desc, 0, off, sizeof(struct ext2_inode), ref) : 0;
}

int ext2_inode_write(struct ext2 *desc, ino_t ino, struct ext2_inode *ext2_inode)
{
    off_t off = ext2_inode_off(desc, ino);

    if (off < 0)
        return off;

    return ext2_io(desc, 1, off, sizeof(struct ext2_inode), ext2_inode);
}

/*
 * The tree block `idx' of a file is in: its root's slot in the inode (0 to
 * 11 the direct pointers, 12 to 14 the indirect ones), its depth (0: the
 * slot is the block's, 1 to 3: the levels of indirect blocks above it)
 * and the block's index in it; -EFBIG past them
 */
#define SLOT_SINGLY     EXT2_DIRECT_POINTERS

static int block_tree(struct ext2 *desc, size_t idx, int *slot, size_t *rel)
{
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx < EXT2_DIRECT_POINTERS) {
        *slot = idx;
        *rel = 0;
        return 0;
    }

    idx -= EXT2_DIRECT_POINTERS;

    for (size_t depth = 1, n = p; depth <= 3; ++depth, n *= p) {
        if (idx < n) {
            *slot = SLOT_SINGLY + depth - 1;
            *rel = idx;
            return depth;
        }

        idx -= n;
    }

    return -EFBIG;
}

/** the pointer in slot `slot' of the inode (it's packed: no pointers to
 * its fields) */
static uint32_t slot_get(struct ext2_inode *inode, int slot)
{
    switch (slot) {
        case SLOT_SINGLY:     return inode->singly_indirect_pointer;
        case SLOT_SINGLY + 1: return inode->doubly_indirect_pointer;
        case SLOT_SINGLY + 2: return inode->triply_indirect_pointer;
        default:              return inode->direct_pointer[slot];
    }
}

static void slot_set(struct ext2_inode *inode, int slot, uint32_t block)
{
    switch (slot) {
        case SLOT_SINGLY:     inode->singly_indirect_pointer = block; break;
        case SLOT_SINGLY + 1: inode->doubly_indirect_pointer = block; break;
        case SLOT_SINGLY + 2: inode->triply_indirect_pointer = block; break;
        default:              inode->direct_pointer[slot] = block; break;
    }
}

/** the data blocks under an indirect block `depth' levels above them */
static size_t tree_span(struct ext2 *desc, int depth)
{
    size_t span = 1;

    while (depth-- > 1)
        span *= desc->bs / 4;

    return span;
}

/** block `idx' of a file into `buf' (zeros where it's sparse) */
int ext2_inode_block_read(struct ext2 *desc, struct ext2_inode *inode, size_t idx, void *buf)
{
    int slot;
    size_t rel;
    int depth = block_tree(desc, idx, &slot, &rel), err = 0;

    if (depth < 0)
        return depth;

    uint32_t block = slot_get(inode, slot);

    if (depth && block) {
        uint32_t *table = kmalloc(desc->bs, &M_BUFFER, 0);

        if (!table)
            return -ENOMEM;

        for (; depth && block; --depth) {
            size_t span = tree_span(desc, depth);

            if ((err = ext2_block_read(desc, block, table)))
                break;

            block = table[rel / span];
            rel %= span;
        }

        kfree(table);

        if (err)
            return err;
    }

    if (!block) {   /* sparse block */
        memset(buf, 0, desc->bs);
        return 0;
    }

    return ext2_block_read(desc, block, buf);
}

/** allocate a block owned by the inode, i_blocks counts 512-byte sectors */
static int ext2_inode_block_alloc(struct ext2 *desc, struct ext2_inode *inode, uint32_t *block)
{
    int err;

    if ((err = ext2_block_alloc(desc, block)))
        return err;

    inode->blocks += desc->bs / 512;
    return 0;
}

/** get the block at idx of an indirect block, allocating it if needed */
static int ext2_indirect_block_get(struct ext2 *desc, struct ext2_inode *inode, uint32_t table, size_t idx, uint32_t *block)
{
    int err = 0;
    uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);

    if (!tmp)
        return -ENOMEM;

    if ((err = ext2_block_read(desc, table, tmp)))
        goto done;

    if (!(*block = tmp[idx])) {    /* Allocate */
        if (!(err = ext2_inode_block_alloc(desc, inode, block))) {
            tmp[idx] = *block;
            err = ext2_block_write(desc, table, tmp);
        }
    }

done:
    kfree(tmp);
    return err;
}

/** `buf' as block `idx' of a file, the blocks it needs allocated */
int ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf)
{
    uint32_t nblocks = inode->blocks;
    int slot;
    size_t rel;
    int depth = block_tree(desc, idx, &slot, &rel), err = 0;

    if (depth < 0)
        return depth;

    uint32_t block = slot_get(inode, slot);

    if (!block && !(err = ext2_inode_block_alloc(desc, inode, &block)))
        slot_set(inode, slot, block);

    for (; !err && depth; --depth) {
        size_t span = tree_span(desc, depth);
        uint32_t next = 0;

        err = ext2_indirect_block_get(desc, inode, block, rel / span, &next);
        block = next;
        rel %= span;
    }

    /* persist new block pointers and i_blocks */
    if (inode->blocks != nblocks) {
        int werr = ext2_inode_write(desc, inode_nr, inode);
        err = err ? err : werr;
    }

    if (err)
        return err;

    return ext2_block_write(desc, block, buf);
}

/** a free inode, marked used, into `*ino': 0, -ENOSPC, or an error (a
 * bitmap that couldn't be read isn't written back) */
int ext2_inode_alloc(struct ext2 *desc, ino_t *ino)
{
    vfs_log(LOG_DEBUG, "ext2_inode_alloc(desc=%p)\n", desc);

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    uint32_t idx = 0, group = 0;
    struct bitmap bm = {0};
    int err = -ENOSPC;

    if (!buf)
        return -ENOMEM;

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        if (!desc->groups[i].free_inodes)
            continue;

        if ((err = ext2_block_read(desc, desc->groups[i].imap, buf)))
            goto done;

        bm.map = buf;
        bm.max_idx = desc->superblock->ginodes;

        /* look for a free inode */
        for (idx = 0; idx < bm.max_idx && bitmap_check(&bm, idx); ++idx);

        if (idx < bm.max_idx) {
            group = i;
            err = 0;
            break;
        }

        err = -ENOSPC;
    }

    if (err)
        goto done;

    /* Update bitmap */
    bitmap_set(&bm, idx);

    if ((err = ext2_block_write(desc, desc->groups[group].imap, buf)))
        goto done;

    /* Update block group descriptor, and super block */
    desc->groups[group].free_inodes--;
    desc->superblock->free_inodes--;

    if (!(err = ext2_bgd_table_rewrite(desc)) && !(err = ext2_superblock_rewrite(desc)))
        *ino = idx + group * desc->superblock->ginodes + 1;

done:
    kfree(buf);
    return err;
}
