/** \file
 * minix: allocating and freeing data blocks (zones), and reading and
 * writing them, through the block cache for the ones read often.
 */

#include <ds/bitmap.h>
#include <minix.h>

/**
 * Allocates a free zone: its bit is set in the zone bitmap, which is
 * written back to the disk at once.
 *
 * @param desc  the mounted filesystem
 * @return the zone's block number, or 0 if the filesystem is full
 */
blk_t minix_block_alloc(struct minix *desc)
{
    /* look for a free zone */
    blk_t blk = 0;
    for (; blk < desc->zmap.max_idx && bitmap_check(&desc->zmap, blk); ++blk);

    /* out of zones */
    if (blk == desc->zmap.max_idx)
        return 0;

    bitmap_set(&desc->zmap, blk);

    /* write the bitmap back now (it could be deferred) */
    vfs_write(desc->supernode, desc->zmap_off, desc->superblock->zmap_blocks * desc->bs, desc->zmap.map);

    return desc->superblock->firstdatazone + blk - 1;
}

/**
 * Frees a zone: its bit is cleared in the zone bitmap, which is written
 * back at once, and its copy in the block cache goes.
 *
 * @param desc  the mounted filesystem
 * @param blk   the zone's block number
 */
void minix_block_free(struct minix *desc, blk_t blk)
{
    blk = blk - desc->superblock->firstdatazone + 1;

    /* not a data zone: nothing to free */
    if (blk >= desc->zmap.max_idx)
        return;

    /* clear its bit; a cached copy of the block (an indirect zone's
     * table) goes too */
    bitmap_clear(&desc->zmap, blk);
    bcache_remove(desc->bcache, blk + desc->superblock->firstdatazone - 1);

    /* write the bitmap back now (it could be deferred) */
    vfs_write(desc->supernode, desc->zmap_off, desc->superblock->zmap_blocks * desc->bs, desc->zmap.map);
}

/**
 * Reads a block from the disk.
 *
 * @param desc  the mounted filesystem
 * @param blk   the block number
 * @param buf   where it goes, a block's size
 * @return the number of bytes read, or a negative errno
 */
ssize_t minix_block_read(struct minix *desc, blk_t blk, void *buf)
{
    return vfs_read(desc->supernode, blk * desc->bs, desc->bs, buf);
}

/**
 * Writes a block to the disk.
 *
 * @param desc  the mounted filesystem
 * @param blk   the block number
 * @param buf   the block's new contents
 * @return the number of bytes written, or a negative errno
 */
ssize_t minix_block_write(struct minix *desc, blk_t blk, void *buf)
{
    return vfs_write(desc->supernode, blk * desc->bs, desc->bs, buf);
}

/**
 * Gets a block from the block cache, reading it from the disk into the
 * cache if it isn't there. The cache keeps the block's memory.
 *
 * @param desc  the mounted filesystem
 * @param blk   the block number
 * @param data  set to the cached block
 * @return 0, `-ENOMEM`, or a negative errno from the read
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
