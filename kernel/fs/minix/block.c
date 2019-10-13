#include <ds/bitmap.h>
#include <minix.h>

/**
 * \ingroup fs-minix
 * \brief allocate a new block
 */
blk_t minix_block_alloc(struct minix *desc)
{
    /* look for a free zone */
    blk_t blk = 0;
    for (; blk < desc->zmap.max_idx && bitmap_check(&desc->zmap, blk); ++blk);

    /* out of zones */
    if (blk == desc->zmap.max_idx)
        return 0;

    /* update bitmap */
    bitmap_set(&desc->zmap, blk);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->zmap_off, desc->superblock->zmap_blocks * desc->bs, desc->zmap.map);

    return desc->superblock->firstdatazone + blk - 1;
}

/**
 * \ingroup fs-minix
 * \brief free a block
 */
void minix_block_free(struct minix *desc, blk_t blk)
{
    blk = blk - desc->superblock->firstdatazone + 1;

    if (blk >= desc->zmap.max_idx)
        /* TODO warn */
        return;

    /* update bitmap */
    bitmap_clear(&desc->zmap, blk);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->zmap_off, desc->superblock->zmap_blocks * desc->bs, desc->zmap.map);
}

/**
 * \ingroup fs-minix
 * \brief read a block from device
 */
ssize_t minix_block_read(struct minix *desc, blk_t blk, void *buf)
{
    return vfs_read(desc->supernode, blk * desc->bs, desc->bs, buf);
}

/**
 * \ingroup fs-minix
 * \brief write a block to device
 */
ssize_t minix_block_write(struct minix *desc, blk_t blk, void *buf)
{
    return vfs_write(desc->supernode, blk * desc->bs, desc->bs, buf);
}

/**
 * \ingroup fs-minix
 * \brief get a block from block cache, cache in if not found
 */
int minix_bcache_get(struct minix *desc, blk_t blk, void **data)
{
    int err = 0;
    void *r = NULL;

    if ((r = bcache_find(desc->bcache, blk))) {
        *data = r;
        return 0;
    }

    r = kmalloc(desc->bs, &M_BUFFER, 0);
    if (!r) goto e_nomem;

    if ((err = minix_block_read(desc, blk, r)) < 0)
        goto error;

    bcache_insert(desc->bcache, blk, r);

    *data = r;
    return 0;

e_nomem:
    err = -ENOMEM;

error:
    return err;
}
