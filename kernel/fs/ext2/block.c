#include <ext2.h>
#include <ds/bitmap.h>

void ext2_bgd_table_rewrite(struct ext2 *desc)
{
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_size = desc->ngroups * sizeof(struct ext2_group);
    vfs_write(desc->supernode, bgd_table, bgds_size, desc->groups);
}

void *ext2_block_read(struct ext2 *desc, uint32_t number, void *buf)
{
    vfs_read(desc->supernode, number * desc->bs, desc->bs, buf);
    return buf;
}

void *ext2_block_write(struct ext2 *desc, uint32_t number, void *buf)
{
    vfs_write(desc->supernode, number * desc->bs, desc->bs, buf);
    return buf;
}

uint32_t ext2_block_alloc(struct ext2 *desc)
{
    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    uint32_t block = 0, real_block = 0, group = 0;
    struct bitmap bm = {0};
    int found = 0;

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        block = 0;

        if (desc->groups[i].free_blocks) {
            ext2_block_read(desc, desc->groups[i].bmap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->gblocks;
            
            /* Look for a free block */
            for (; block < bm.max_idx && bitmap_check(&bm, block); ++block);

            if (block == bm.max_idx)
                continue;

            group = i;

            /* block bitmaps start at the first data block (1 for 1K blocks) */
            real_block = desc->superblock->dblock + block + group * desc->superblock->gblocks;
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Out of space */
        kfree(buf);
        return 0;
    }

    /* Update bitmap */
    bitmap_set(&bm, block);
    ext2_block_write(desc, desc->groups[group].bmap, buf);

    /* Update block group descriptor */
    desc->groups[group].free_blocks--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->free_blocks--;
    ext2_superblock_rewrite(desc);

    /* Clear block */
    memset(buf, 0, desc->bs);
    ext2_block_write(desc, real_block, buf);


    kfree(buf);
    return real_block;
}

void ext2_block_free(struct ext2 *desc, uint32_t block)
{
    /* block bitmaps start at the first data block (1 for 1K blocks) */
    if (block < desc->superblock->dblock || block >= desc->superblock->nblocks)
        return;

    uint32_t rel   = block - desc->superblock->dblock;
    uint32_t group = rel / desc->superblock->gblocks;

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct bitmap bm = {0};

    ext2_block_read(desc, desc->groups[group].bmap, buf);

    bm.map = buf;
    uint32_t block_idx = rel % desc->superblock->gblocks;

    /* Update bitmap */
    bitmap_clear(&bm, block_idx);
    ext2_block_write(desc, desc->groups[group].bmap, buf);

    /* Update block group descriptor */
    desc->groups[group].free_blocks++;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->free_blocks++;
    ext2_superblock_rewrite(desc);


    kfree(buf);
}
