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

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        if (desc->groups[i].free_blocks) {
            ext2_block_read(desc, desc->groups[i].bmap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->gblocks;
            
            /* Look for a free block */
            for (; bitmap_check(&bm, block); ++block);
            group = i;
            real_block = block + group * desc->superblock->gblocks;
            break;
        }
    }

    if (!block) {
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
    uint32_t group = block % desc->superblock->gblocks;

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct bitmap bm = {0};

    ext2_block_read(desc, desc->groups[group].bmap, buf);

    bm.map = buf;
    uint32_t block_idx = block - group * desc->superblock->gblocks;

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
