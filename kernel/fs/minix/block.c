#include <ds/bitmap.h>
#include <minix.h>

ssize_t minix_block_read(struct minix *desc, uint32_t number, void *buf)
{
    return vfs_read(desc->supernode, number * desc->bs, desc->bs, buf);
}

ssize_t minix_block_write(struct minix *desc, uint32_t number, void *buf)
{
    return vfs_write(desc->supernode, number * desc->bs, desc->bs, buf);
}

uint32_t minix_block_allocate(struct minix *desc)
{
    char buf[desc->bs];

    uint32_t block_id = 0, idx = 0, block = 0;
    bitmap_t bm = {0};

    for (unsigned i = 0; i < desc->superblock->zmap_blocks; ++i) {
        minix_block_read(desc, desc->zmap / desc->bs + i, &buf);

        bm.map = (uint32_t *) &buf;
        bm.max_idx = desc->bs;
        
        /* Look for a free inode */
        for (idx = 0; idx < bm.max_idx && bitmap_check(&bm, idx); ++idx);

        if (idx == bm.max_idx)
            continue;

        block = i;
        block_id = desc->superblock->firstdatazone + idx + i * bm.max_idx;
        break;
    }

    if (!block_id)
        return 0;

    /* Update bitmap */
    bitmap_set(&bm, idx);
    minix_block_write(desc, desc->zmap / desc->bs + block, &buf);

    return block_id;
}
