/** \file
 * ext2: reading and writing the disk, the block group descriptors, and
 * allocating and freeing blocks in the groups' block bitmaps.
 */

#include <ext2.h>
#include <ds/bitmap.h>

/**
 * Reads or writes bytes of the filesystem's disk, all of them.
 *
 * @param desc   the mounted filesystem
 * @param write  whether to write (1) or read (0)
 * @param off    where on the disk, in bytes
 * @param size   how many bytes
 * @param buf    the data
 * @return 0, the device's error, or `-EIO` if fewer bytes went
 */
int ext2_io(struct ext2 *desc, int write, off_t off, size_t size, void *buf)
{
    ssize_t r = write ? vfs_write(desc->supernode, off, size, buf) :
                        vfs_read(desc->supernode, off, size, buf);

    return r == (ssize_t) size ? 0 : r < 0 ? (int) r : -EIO;
}

/**
 * Writes the block group descriptor table back to the disk, after a
 * change to a group's counts.
 *
 * @param desc  the mounted filesystem
 * @return 0, or a negative errno
 */
int ext2_bgd_table_rewrite(struct ext2 *desc)
{
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_size = desc->ngroups * sizeof(struct ext2_group);
    return ext2_io(desc, 1, bgd_table, bgds_size, desc->groups);
}

/**
 * Reads a block from the disk.
 *
 * @param desc    the mounted filesystem
 * @param number  the block number
 * @param buf     where it goes, a block's size
 * @return 0, or a negative errno
 */
int ext2_block_read(struct ext2 *desc, uint32_t number, void *buf)
{
    return ext2_io(desc, 0, (off_t) number * desc->bs, desc->bs, buf);
}

/**
 * Writes a block to the disk.
 *
 * @param desc    the mounted filesystem
 * @param number  the block number
 * @param buf     the block's new contents
 * @return 0, or a negative errno
 */
int ext2_block_write(struct ext2 *desc, uint32_t number, void *buf)
{
    return ext2_io(desc, 1, (off_t) number * desc->bs, desc->bs, buf);
}

/**
 * Allocates a free block: its bit is set in its group's block bitmap, the
 * free counts go down, and the block is zeroed. A bitmap that couldn't be
 * read isn't written back.
 *
 * @param desc   the mounted filesystem
 * @param block  set to the block's number
 * @return 0, `-ENOSPC` if the filesystem is full, `-ENOMEM`, or an I/O
 *         error
 */
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

        /* look for a free block in this group */
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

    bitmap_set(&bm, idx);

    if ((err = ext2_block_write(desc, desc->groups[group].bmap, buf)))
        goto done;

    /* one block fewer free, in the group and the filesystem */
    desc->groups[group].free_blocks--;
    desc->superblock->free_blocks--;

    if ((err = ext2_bgd_table_rewrite(desc)) || (err = ext2_superblock_rewrite(desc)))
        goto done;

    /* block bitmaps start at the first data block (1 for 1K blocks) */
    *block = desc->superblock->dblock + idx + group * desc->superblock->gblocks;

    /* a new block starts zeroed */
    memset(buf, 0, desc->bs);
    err = ext2_block_write(desc, *block, buf);

done:
    kfree(buf);
    return err;
}

/**
 * Frees a block: its bit is cleared in its group's block bitmap, and the
 * free counts go up. If the bitmap can't be read, the block stays used.
 *
 * @param desc   the mounted filesystem
 * @param block  the block's number
 * @return 0, `-EINVAL` for a block outside the filesystem, `-ENOMEM`, or
 *         an I/O error
 */
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

    bitmap_clear(&bm, block_idx);

    if ((err = ext2_block_write(desc, desc->groups[group].bmap, buf)))
        goto done;

    /* one block more free, in the group and the filesystem */
    desc->groups[group].free_blocks++;
    desc->superblock->free_blocks++;

    if (!(err = ext2_bgd_table_rewrite(desc)))
        err = ext2_superblock_rewrite(desc);

done:
    kfree(buf);
    return err;
}
