/** \file
 * minix: inodes, and the zones of a file. A file's zones are found through
 * its inode's 7 direct zones, then a singly and a doubly indirect one: a
 * tree of zones of 16-bit zone numbers. The indirect zones are read
 * through the block cache.
 */

#include <ds/bitmap.h>
#include <minix.h>

/** Returns where an inode is on the disk, in bytes, or `-EINVAL` for an
 * inode number that doesn't exist. */
static inline off_t minix_inode_off(struct minix *desc, ino_t ino)
{
    /* inodes are numbered from 1 to ninodes */
    if (!ino || ino > desc->superblock->ninodes)
        return -EINVAL;

    return desc->inode_table + (ino - 1) * desc->inode_size;
}

/**
 * Reads an inode from the disk.
 *
 * @param desc  the mounted filesystem
 * @param ino   the inode number
 * @param ref   where it goes; NULL only checks the number
 * @return 0, `-EINVAL` for a bad inode number or a short read, or an I/O
 *         error
 */
int minix_inode_read(struct minix *desc, ino_t ino, struct minix_inode *ref)
{
    off_t off;

    if ((off = minix_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
        size_t size = sizeof(struct minix_inode);
        ssize_t r;

        if ((r = vfs_read(desc->supernode, off, size, ref)) != (ssize_t) size) {
            if (r < 0)
                return r;

            /* a short read */
            return -EINVAL;
        }
    }

    return 0;
}

/**
 * Writes an inode to the disk.
 *
 * @param desc  the mounted filesystem
 * @param ino   the inode number
 * @param ref   the inode; NULL only checks the number
 * @return 0, `-EINVAL` for a bad inode number or a short write, or an I/O
 *         error
 */
int minix_inode_write(struct minix *desc, ino_t ino, struct minix_inode *ref)
{
    off_t off;

    if ((off = minix_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
        size_t size = sizeof(struct minix_inode);
        ssize_t r;

        if ((r = vfs_write(desc->supernode, off, size, ref)) != (ssize_t) size) {
            if (r < 0)
                return r;

            /* a short write */
            return -EINVAL;
        }
    }

    return 0;
}

/**
 * Finds where zone `idx` of a file is in its tree: the slot of `zones[]`
 * it hangs from (0 to 6 for the direct zones, 7 for the singly indirect
 * one, 8 for the doubly indirect one) in `*slot`, and its index under that
 * slot in `*rel`. Returns the depth: 0 if the slot is the zone itself, 1
 * or 2 for the levels of indirect zones above it, or `-EFBIG` past the
 * largest file.
 */
static int zone_tree(struct minix *desc, size_t idx, int *slot, size_t *rel)
{
    size_t p = desc->bs / sizeof(uint16_t);     /* pointers per zone */

    if (idx < MINIX_DIRECT_ZONES) {
        *slot = idx;
        *rel = 0;
        return 0;
    }

    idx -= MINIX_DIRECT_ZONES;

    if (idx < p) {
        *slot = MINIX_DIRECT_ZONES;
        *rel = idx;
        return 1;
    }

    idx -= p;

    if (idx < p * p) {
        *slot = MINIX_DIRECT_ZONES + 1;
        *rel = idx;
        return 2;
    }

    return -EFBIG;
}

/** Returns how many data zones are under one pointer of an indirect zone
 * that is `depth` levels above them. */
static size_t zone_span(struct minix *desc, int depth)
{
    return depth == 2 ? desc->bs / sizeof(uint16_t) : 1;
}

/**
 * Reads a zone of a file. A zone that was never written (the file is
 * sparse there) reads as zeros.
 *
 * @param desc     the mounted filesystem
 * @param m_inode  the file's inode
 * @param idx      the zone's index in the file
 * @param buf      where it goes, a block's size
 * @return 0, `-EINVAL` past the end of the file, `-EFBIG`, or an I/O
 *         error
 */
ssize_t minix_inode_block_read(struct minix *desc, struct minix_inode *m_inode, size_t idx, void *buf)
{
    size_t bs = desc->bs;
    size_t nr_blocks = (m_inode->size + bs - 1) / bs;
    int slot, err;
    size_t rel;

    if (idx >= nr_blocks)
        return -EINVAL;

    int depth = zone_tree(desc, idx, &slot, &rel);

    if (depth < 0)
        return depth;

    blk_t zone = m_inode->zones[slot];

    for (; depth && zone; --depth) {
        size_t span = zone_span(desc, depth);
        uint16_t *table;

        if ((err = minix_bcache_get(desc, zone, (void **) &table)))
            return err;

        zone = table[rel / span];
        rel %= span;
    }

    if (!zone) {    /* a hole in a sparse file */
        memset(buf, 0, bs);
        return 0;
    }

    ssize_t r = minix_block_read(desc, zone, buf);
    return r == (ssize_t) bs ? 0 : r < 0 ? r : -EIO;
}

/** Allocates a zone for an indirect table, zeroed on the disk, and drops
 * any cached copy of it (the zone may have been a table before). */
static int table_alloc(struct minix *desc, blk_t *zone)
{
    if (!(*zone = minix_block_alloc(desc)))
        return -ENOSPC;

    uint16_t *zero = kmalloc(desc->bs, &M_BUFFER, M_ZERO);

    if (!zero)
        return -ENOMEM;

    ssize_t r = minix_block_write(desc, *zone, zero);
    kfree(zero);

    bcache_remove(desc->bcache, *zone);
    return r == (ssize_t) desc->bs ? 0 : r < 0 ? r : -EIO;
}

/**
 * Writes a zone of a file, allocating it and the indirect zones above it
 * if they don't exist yet. The inode is written back if its zones changed.
 *
 * @param desc   the mounted filesystem
 * @param inode  the file's inode
 * @param ino    its inode number
 * @param idx    the zone's index in the file
 * @param buf    the zone's new contents
 * @return 0, `-EFBIG`, `-ENOSPC`, `-ENOMEM`, or an I/O error
 */
ssize_t minix_inode_block_write(struct minix *desc, struct minix_inode *inode, ino_t ino, size_t idx, void *buf)
{
    int slot, err = 0, changed = 0;
    size_t rel;
    int depth = zone_tree(desc, idx, &slot, &rel);

    if (depth < 0)
        return depth;

    blk_t zone = inode->zones[slot];

    if (!zone) {
        if (depth)
            err = table_alloc(desc, &zone);
        else if (!(zone = minix_block_alloc(desc)))
            err = -ENOSPC;

        if (!err) {
            inode->zones[slot] = zone;
            changed = 1;
        }
    }

    /* go down the tables, through the cache, so that readers see the new
     * pointers */
    for (; !err && depth; --depth) {
        size_t span = zone_span(desc, depth);
        uint16_t *table;
        blk_t next;

        if ((err = minix_bcache_get(desc, zone, (void **) &table)))
            break;

        if (!(next = table[rel / span])) {
            if (depth > 1)
                err = table_alloc(desc, &next);
            else if (!(next = minix_block_alloc(desc)))
                err = -ENOSPC;

            if (err)
                break;

            /* allocating the new table may have dropped this table's copy
             * from the cache: get it again */
            if ((err = minix_bcache_get(desc, zone, (void **) &table)))
                break;

            table[rel / span] = next;
            minix_block_write(desc, zone, table);
        }

        zone = next;
        rel %= span;
    }

    if (changed) {
        int werr = minix_inode_write(desc, ino, inode);
        err = err ? err : werr;
    }

    if (err)
        return err;

    ssize_t r = minix_block_write(desc, zone, buf);
    return r == (ssize_t) desc->bs ? 0 : r < 0 ? r : -EIO;
}

/**
 * Frees the zones of the tree at `zone` (a data zone at depth 0, or an
 * indirect zone `depth` levels above the data), from data zone `from` of
 * the tree on. Returns the zone, or 0 if it was freed. A table that can't
 * be read stays, with what's under it.
 */
static blk_t free_tree(struct minix *desc, blk_t zone, int depth, size_t from)
{
    if (!zone)
        return 0;

    if (depth) {
        size_t span = zone_span(desc, depth), p = desc->bs / sizeof(uint16_t);
        uint16_t *table;

        if (minix_bcache_get(desc, zone, (void **) &table))
            return zone;

        for (size_t i = from / span; i < p; ++i)
            table[i] = free_tree(desc, table[i], depth - 1, i == from / span ? from % span : 0);

        /* the first part of the tree stays */
        if (from) {
            minix_block_write(desc, zone, table);
            return zone;
        }
    }

    minix_block_free(desc, zone);
    return 0;
}

/**
 * Frees a file's zones, from a zone on, when the file is truncated or
 * removed.
 *
 * @param desc   the mounted filesystem
 * @param inode  the file's inode, its zone pointers cleared here
 * @param from   the index of the first zone to free
 */
void minix_zones_free(struct minix *desc, struct minix_inode *inode, size_t from)
{
    size_t p = desc->bs / sizeof(uint16_t), base = MINIX_DIRECT_ZONES;

    /* a device node keeps its number in its first zone: it has no zones */
    if (S_ISCHR(inode->mode) || S_ISBLK(inode->mode))
        return;

    for (size_t i = from; i < MINIX_DIRECT_ZONES; ++i)
        inode->zones[i] = free_tree(desc, inode->zones[i], 0, 0);

    inode->zones[MINIX_DIRECT_ZONES] =
        free_tree(desc, inode->zones[MINIX_DIRECT_ZONES], 1, from > base ? from - base : 0);
    base += p;
    inode->zones[MINIX_DIRECT_ZONES + 1] =
        free_tree(desc, inode->zones[MINIX_DIRECT_ZONES + 1], 2, from > base ? from - base : 0);
}

/**
 * Allocates a free inode: its bit is set in the inode bitmap, which is
 * written back to the disk at once.
 *
 * @param desc  the mounted filesystem
 * @return the inode number, or 0 if there's no free inode
 */
ino_t minix_inode_alloc(struct minix *desc)
{
    /* look for a free inode: bit i of the bitmap is inode i, from 1 to
     * ninodes (bit 0 is reserved) */
    ino_t ino = 1;
    for (; ino <= desc->imap.max_idx && bitmap_check(&desc->imap, ino); ++ino);

    /* out of inodes */
    if (ino > desc->imap.max_idx)
        return 0;

    bitmap_set(&desc->imap, ino);

    /* write the bitmap back now (it could be deferred) */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);

    return ino;
}

/**
 * Frees an inode: its bit is cleared in the inode bitmap, which is
 * written back at once.
 *
 * @param desc  the mounted filesystem
 * @param ino   the inode number
 */
void minix_inode_free(struct minix *desc, ino_t ino)
{
    /* not an inode: nothing to free */
    if (!ino || ino > desc->imap.max_idx)
        return;

    bitmap_clear(&desc->imap, ino);

    /* write the bitmap back now (it could be deferred) */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);
}
