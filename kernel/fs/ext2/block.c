#include <ext2.h>
#include <ds/bitmap.h>

void ext2_bgd_table_rewrite(struct ext2 *desc)
{
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_size = desc->bgds_count * sizeof(struct ext2_block_group_descriptor);
    vfs_write(desc->supernode, bgd_table, bgds_size, desc->bgd_table);
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

uint32_t ext2_block_allocate(struct ext2 *desc)
{
    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
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
