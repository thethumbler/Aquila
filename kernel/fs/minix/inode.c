#include <ds/bitmap.h>
#include <minix.h>

static inline off_t minix_inode_off(struct minix *desc, ino_t ino)
{
    /* invalid inode */
    if (!ino || ino >= desc->superblock->ninodes)
        return -EINVAL;

    return desc->inode_table + (ino - 1) * desc->inode_size;
}

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

            /* FIXME Check for further errors? */
            return -EINVAL;
        }
    }

    return 0;
}

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

            /* FIXME Check for further errors? */
            return -EINVAL;
        }
    }

    return 0;
}

/**
 * The tree zone `idx' of a file is in: its root's slot in zones[] (0 to 6
 * the direct zones, 7 the singly indirect one, 8 the doubly), its depth
 * (0: the slot is the zone's, 1 or 2: the levels of indirect zones above
 * it) and the zone's index in it; -EFBIG past them
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

/** the data zones under an indirect zone `depth' levels above them */
static size_t zone_span(struct minix *desc, int depth)
{
    return depth == 2 ? desc->bs / sizeof(uint16_t) : 1;
}

/** zone `idx' of a file into `buf' (zeros where it's sparse) */
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

    if (!zone) {    /* sparse */
        memset(buf, 0, bs);
        return 0;
    }

    ssize_t r = minix_block_read(desc, zone, buf);
    return r == (ssize_t) bs ? 0 : r < 0 ? r : -EIO;
}

/** a new zone for an indirect table: zeroed, on the disk and in the cache
 * (it may have been one before) */
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

/** `buf' as zone `idx' of a file, the zones it needs allocated */
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

    /* down the tables, through the cache (readers see the pointers) */
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

            /* the cache's copy may have gone for the new table's */
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

/** free the zones of the tree at `zone' (a data zone at depth 0, else an
 * indirect zone `depth' levels above the data), from data zone `from' of
 * the tree on; returns the zone, 0 if it was freed (a table that can't be
 * read stays, with what's under it) */
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

        /* part of the tree stays */
        if (from) {
            minix_block_write(desc, zone, table);
            return zone;
        }
    }

    minix_block_free(desc, zone);
    return 0;
}

/** free the zones of `inode' from data zone `from' on */
void minix_zones_free(struct minix *desc, struct minix_inode *inode, size_t from)
{
    size_t p = desc->bs / sizeof(uint16_t), base = MINIX_DIRECT_ZONES;

    /* a device node keeps its number in its first zone: no zones */
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

ino_t minix_inode_alloc(struct minix *desc)
{
    /* look for a free inode */
    ino_t ino = 0;
    for (; ino < desc->imap.max_idx && bitmap_check(&desc->imap, ino); ++ino);

    /* out of inodes */
    if (ino == desc->imap.max_idx)
        return 0;

    /* update bitmap */
    bitmap_set(&desc->imap, ino);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);

    return ino;
}

void minix_inode_free(struct minix *desc, ino_t ino)
{
    if (ino >= desc->imap.max_idx)
        /* TODO warn */
        return;

    /* update bitmap */
    bitmap_clear(&desc->imap, ino);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);
}
