#include <ext2.h>
#include <ds/bitmap.h>

/* `size' bytes at `off' of the device, all of them (`write': to it): 0,
 * or the error, -EIO if fewer went */
int ext2_io(struct ext2 *desc, int write, off_t off, size_t size, void *buf)
{
    ssize_t r = write ? vfs_write(desc->supernode, off, size, buf) :
                        vfs_read(desc->supernode, off, size, buf);

    return r == (ssize_t) size ? 0 : r < 0 ? (int) r : -EIO;
}

int ext2_bgd_table_rewrite(struct ext2 *desc)
{
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_size = desc->ngroups * sizeof(struct ext2_group);
    return ext2_io(desc, 1, bgd_table, bgds_size, desc->groups);
}

int ext2_block_read(struct ext2 *desc, uint32_t number, void *buf)
{
    return ext2_io(desc, 0, (off_t) number * desc->bs, desc->bs, buf);
}

int ext2_block_write(struct ext2 *desc, uint32_t number, void *buf)
{
    return ext2_io(desc, 1, (off_t) number * desc->bs, desc->bs, buf);
}

/* a free block, marked used and zeroed, into `*block': 0, -ENOSPC, or
 * an error (a bitmap that couldn't be read isn't written back) */
int ext2_block_alloc(struct ext2 *desc, uint32_t *block)
{
    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    uint32_t idx = 0, group = 0;
    struct bitmap bm = {0};
    int err = -ENOSPC;

    if (!buf)
        return -ENOMEM;

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        if (!desc->groups[i].free_blocks)
            continue;

        if ((err = ext2_block_read(desc, desc->groups[i].bmap, buf)))
            goto done;

        bm.map = buf;
        bm.max_idx = desc->superblock->gblocks;

        /* Look for a free block */
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

    if ((err = ext2_block_write(desc, desc->groups[group].bmap, buf)))
        goto done;

    /* Update block group descriptor, and super block */
    desc->groups[group].free_blocks--;
    desc->superblock->free_blocks--;

    if ((err = ext2_bgd_table_rewrite(desc)) || (err = ext2_superblock_rewrite(desc)))
        goto done;

    /* block bitmaps start at the first data block (1 for 1K blocks) */
    *block = desc->superblock->dblock + idx + group * desc->superblock->gblocks;

    /* Clear block */
    memset(buf, 0, desc->bs);
    err = ext2_block_write(desc, *block, buf);

done:
    kfree(buf);
    return err;
}

/* `block' marked free: 0, or an error (the bitmap that couldn't be read
 * isn't written back: the block stays used) */
int ext2_block_free(struct ext2 *desc, uint32_t block)
{
    /* block bitmaps start at the first data block (1 for 1K blocks) */
    if (block < desc->superblock->dblock || block >= desc->superblock->nblocks)
        return -EINVAL;

    uint32_t rel   = block - desc->superblock->dblock;
    uint32_t group = rel / desc->superblock->gblocks;

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct bitmap bm = {0};
    int err;

    if (!buf)
        return -ENOMEM;

    if ((err = ext2_block_read(desc, desc->groups[group].bmap, buf)))
        goto done;

    bm.map = buf;
    uint32_t block_idx = rel % desc->superblock->gblocks;

    /* Update bitmap */
    bitmap_clear(&bm, block_idx);

    if ((err = ext2_block_write(desc, desc->groups[group].bmap, buf)))
        goto done;

    /* Update block group descriptor, and super block */
    desc->groups[group].free_blocks++;
    desc->superblock->free_blocks++;

    if (!(err = ext2_bgd_table_rewrite(desc)))
        err = ext2_superblock_rewrite(desc);

done:
    kfree(buf);
    return err;
}
