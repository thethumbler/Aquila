/*
 * Block cache: what disks read and write, kept in 4 KiB blocks. Writes
 * stay in it (write back) until blkcache_flush(): sync(), restarting or
 * powering off, too much written (DIRTY_MAX), or written a while ago
 * (DIRTY_AGE) at the next use of the cache. Each sector of a block is
 * valid (it has the disk's data, or newer) or not, and dirty (newer than
 * the disk's) or not: a write doesn't read the rest of its block. Once
 * the cache is full, the clean block used least recently makes room.
 *
 * Drivers use it with absolute sector numbers, after partition offsets,
 * so a sector is in it once whatever device node it was read through.
 * Blocks not cached that a read needs are read with one command, adjacent
 * dirty blocks written with one.
 *
 * The device's transfers are to and from buffers of the cache's, never
 * the caller's: that may be a process's, which isn't mapped until it is
 * used, and mapping it may read the disk (a driver waiting for itself).
 * The caller's buffer is used once its pages are mapped.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <dev/blkcache.h>
#include <aquila/errno.h>

MALLOC_DEFINE(M_BLKCACHE, "blkcache", "block cache");

#define SECTOR_SIZE     512
#define BLK_SECTORS     8
#define BLK_SIZE        (BLK_SECTORS * SECTOR_SIZE)
#define BLK_ALL         0xFF    /* the sectors of a block, as a mask */

/* 8 MiB, in one allocation (the kernel heap is a list) */
#define CACHE_BLOCKS    2048
#define CACHE_BUCKETS   1024

/* blocks a command reads or writes at most */
#define RUN_MAX         16

/* dirty blocks at most before they are written, and how long one may be */
#define DIRTY_MAX       (CACHE_BLOCKS / 2)
#define DIRTY_AGE       5000000000ULL   /* ns */

struct blk {
    void *owner;            /* NULL while the slot is free */
    uint64_t nr;            /* block number, sector / BLK_SECTORS */
    uint8_t valid, dirty;   /* sector masks */
    blkcache_io write;      /* how its dirty sectors get to the disk */

    struct blk *hnext;              /* hash chain */
    struct blk *newer, *older;      /* LRU list */
};

static struct blk blks[CACHE_BLOCKS];
static char *arena;

static struct blk *buckets[CACHE_BUCKETS];
static struct blk *newest, *oldest;
static size_t used, dirty;

/* when the oldest dirty block of the cache became dirty */
static uint64_t dirty_since;

/* writes so far: a read that waited across one doesn't cache what it read,
 * it may be older than the write */
static uint64_t writes;

static inline char *blk_data(struct blk *b)
{
    return arena + (b - blks) * BLK_SIZE;
}

static inline uint8_t sector_mask(size_t first, size_t n)
{
    return ((1U << n) - 1) << first;
}

static inline struct blk **bucket(void *owner, uint64_t nr)
{
    return &buckets[(nr ^ ((uintptr_t) owner >> 4)) % CACHE_BUCKETS];
}

static struct blk *lookup(void *owner, uint64_t nr)
{
    for (struct blk *b = *bucket(owner, nr); b; b = b->hnext)
        if (b->owner == owner && b->nr == nr)
            return b;

    return NULL;
}

static void lru_unlink(struct blk *b)
{
    if (b->newer) b->newer->older = b->older; else newest = b->older;
    if (b->older) b->older->newer = b->newer; else oldest = b->newer;
    b->newer = b->older = NULL;
}

static void lru_push(struct blk *b)
{
    b->older = newest;
    b->newer = NULL;

    if (newest) newest->newer = b; else oldest = b;
    newest = b;
}

static void touch(struct blk *b)
{
    if (b != newest) {
        lru_unlink(b);
        lru_push(b);
    }
}

/* a clean block leaves the cache */
static void evict(struct blk *b)
{
    struct blk **p = bucket(b->owner, b->nr);

    while (*p != b)
        p = &(*p)->hnext;

    *p = b->hnext;
    lru_unlink(b);
    b->owner = NULL;
    --used;
}

static void make_dirty(struct blk *b, uint8_t mask, blkcache_io write)
{
    if (!b->dirty && !dirty++)
        dirty_since = arch_rtime_ns();

    b->dirty |= mask;
    b->write = write;
}

static void make_clean(struct blk *b, uint8_t mask)
{
    if (b->dirty && !(b->dirty &= ~mask))
        --dirty;
}

/* a block for (owner, nr), nothing valid in it; NULL if every block is
 * dirty, or there is no memory */
static struct blk *block_new(void *owner, uint64_t nr)
{
    if (!arena && !(arena = kmalloc(CACHE_BLOCKS * BLK_SIZE, &M_BLKCACHE, 0)))
        return NULL;

    struct blk *b = NULL;

    if (used < CACHE_BLOCKS) {
        for (size_t i = 0; i < CACHE_BLOCKS && !b; ++i)
            if (!blks[i].owner)
                b = &blks[i];
    } else {
        /* the clean block used least recently */
        for (struct blk *old = oldest; old && !b; old = old->newer)
            if (!old->dirty)
                b = old;

        if (!b)
            return NULL;

        evict(b);
    }

    b->owner = owner;
    b->nr = nr;
    b->valid = b->dirty = 0;
    b->hnext = *bucket(owner, nr);
    *bucket(owner, nr) = b;
    lru_push(b);
    ++used;

    return b;
}

/* maps the pages of a buffer, a process's isn't until it is used: before
 * the cache is looked at, mapping one may read the disk and change it */
static void prefault(void *buf, size_t size, int write)
{
    volatile char *p = buf, *end = (char *) buf + size;

    for (; p < end; p = (volatile char *) PAGE_ALIGN((uintptr_t) p + PAGE_SIZE)) {
        char c = *p;
        if (write) *p = c;
    }

    if (size) {
        char c = end[-1];
        if (write) end[-1] = c;
    }
}

/* the sectors of block `nr' within [sector, sector + count): from `first'
 * in the block, `n' of them, at `off' in the request */
static void overlap(uint64_t nr, uint64_t sector, size_t count,
                    size_t *first, size_t *n, size_t *off)
{
    uint64_t start = MAX(nr * BLK_SECTORS, sector);
    uint64_t end = MIN((nr + 1) * BLK_SECTORS, sector + count);

    *first = start - nr * BLK_SECTORS;
    *n = end - start;
    *off = start - sector;
}

static inline int full_dirty(struct blk *b)
{
    return b && b->dirty == BLK_ALL;
}

/* writes the dirty sectors of `b' (with the adjacent wholly dirty blocks
 * after it), and they are clean */
static int flush_block(struct blk *b)
{
    void *owner = b->owner;
    blkcache_io write = b->write;
    uint64_t nr = b->nr;
    int err = 0;

    char *tmp = kmalloc(RUN_MAX * BLK_SIZE, &M_BLKCACHE, 0);
    if (!tmp) return -ENOMEM;

    if (full_dirty(b)) {
        /* a run of wholly dirty blocks, with one command */
        size_t run = 0;
        struct blk *r;

        while (run < RUN_MAX && full_dirty(r = lookup(owner, nr + run)) && r->write == write) {
            memcpy(tmp + run * BLK_SIZE, blk_data(r), BLK_SIZE);
            make_clean(r, BLK_ALL);
            ++run;
        }

        if ((err = write(owner, nr * BLK_SECTORS, run * BLK_SECTORS, tmp)) < 0) {
            /* still not on the disk, if the blocks are still cached */
            for (size_t i = 0; i < run; ++i)
                if ((r = lookup(owner, nr + i)))
                    make_dirty(r, BLK_ALL, write);
        }
    } else {
        /* each range of dirty sectors */
        uint8_t mask = b->dirty;

        for (size_t s = 0; s < BLK_SECTORS && !err; ) {
            if (!(mask & (1 << s))) {
                ++s;
                continue;
            }

            size_t n = 0;
            while (s + n < BLK_SECTORS && (mask & (1 << (s + n))))
                ++n;

            uint8_t m = sector_mask(s, n);
            memcpy(tmp, blk_data(b) + s * SECTOR_SIZE, n * SECTOR_SIZE);
            make_clean(b, m);

            if ((err = write(owner, nr * BLK_SECTORS + s, n, tmp)) < 0) {
                struct blk *r = lookup(owner, nr);
                if (r)
                    make_dirty(r, m, write);
            }

            /* b may have left the cache while writing */
            if (!(b = lookup(owner, nr)))
                break;

            s += n;
        }
    }

    kfree(tmp);
    return err < 0 ? err : 0;
}

int blkcache_flush(void)
{
    int err = 0;

    /* in slot order; writing sleeps, the cache may change meanwhile, each
     * block is looked at again */
    for (size_t i = 0; i < CACHE_BLOCKS && dirty; ++i) {
        struct blk *b = &blks[i];

        if (!b->owner || !b->dirty)
            continue;

        /* from the start of its run of wholly dirty blocks */
        if (full_dirty(b)) {
            struct blk *prev;

            for (size_t k = 0; k < RUN_MAX && b->nr && full_dirty(prev = lookup(b->owner, b->nr - 1)) &&
                               prev->write == b->write; ++k)
                b = prev;
        }

        int e = flush_block(b);
        if (e)
            err = e;

        /* the block it was at may have more */
        if (blks[i].owner && blks[i].dirty && !e)
            --i;
    }

    return err;
}

/* dirty blocks written a while ago go to the disk */
static void flush_old(void)
{
    static int flushing;

    if (dirty && !flushing && arch_rtime_ns() - dirty_since > DIRTY_AGE) {
        flushing = 1;
        blkcache_flush();
        flushing = 0;
    }
}

ssize_t blkcache_read(void *owner, uint64_t sector, size_t count, void *buf,
                      uint64_t nsectors, blkcache_io read)
{
    char *out = buf;
    uint64_t last = (sector + count - 1) / BLK_SECTORS;
    size_t first, n, off;

    if (!count)
        return 0;

    flush_old();
    prefault(buf, count * SECTOR_SIZE, 1);

    for (uint64_t nr = sector / BLK_SECTORS; nr <= last; ) {
        struct blk *b = lookup(owner, nr);

        overlap(nr, sector, count, &first, &n, &off);
        uint8_t need = sector_mask(first, n);

        if (b && (b->valid & need) == need) {
            memcpy(out + off * SECTOR_SIZE, blk_data(b) + first * SECTOR_SIZE, n * SECTOR_SIZE);
            touch(b);
            ++nr;
            continue;
        }

        /* the blocks from here that miss sectors, at once */
        size_t run = 1;
        while (nr + run <= last && run < RUN_MAX) {
            struct blk *r = lookup(owner, nr + run);
            size_t f, k, o;

            overlap(nr + run, sector, count, &f, &k, &o);

            if (r && (r->valid & sector_mask(f, k)) == sector_mask(f, k))
                break;

            ++run;
        }

        uint64_t start = nr * BLK_SECTORS;
        size_t sectors = run * BLK_SECTORS;

        /* not past the end of the disk, if it is known */
        if (nsectors && start + sectors > nsectors)
            sectors = start < nsectors ? nsectors - start : 0;

        if (!sectors)
            return -EIO;

        char *tmp = kmalloc(sectors * SECTOR_SIZE, &M_BLKCACHE, 0);

        if (!tmp)
            return -ENOMEM;

        uint64_t w = writes;
        ssize_t err = read(owner, start, sectors, tmp);

        if (err < 0) {
            kfree(tmp);
            return err;
        }

        for (size_t i = 0; i < run; ++i) {
            size_t have = sectors > i * BLK_SECTORS ? MIN(BLK_SECTORS, sectors - i * BLK_SECTORS) : 0;
            char *data = tmp + i * BLK_SIZE;

            overlap(nr + i, sector, count, &first, &n, &off);

            if (first + n > have) {
                kfree(tmp);
                return -EIO;
            }

            /* written meanwhile: the cache has newer sectors than the disk
             * had, the others are filled in; a block that wasn't cached
             * isn't, unless nothing was written meanwhile */
            struct blk *r = lookup(owner, nr + i);

            if (!r && w == writes && (r = block_new(owner, nr + i)))
                r->valid = 0;

            if (r) {
                for (size_t s = 0; s < have; ++s) {
                    if (!(r->valid & (1 << s))) {
                        memcpy(blk_data(r) + s * SECTOR_SIZE, data + s * SECTOR_SIZE, SECTOR_SIZE);
                        r->valid |= 1 << s;
                    }
                }

                memcpy(out + off * SECTOR_SIZE, blk_data(r) + first * SECTOR_SIZE, n * SECTOR_SIZE);
                touch(r);
            } else {
                memcpy(out + off * SECTOR_SIZE, data + first * SECTOR_SIZE, n * SECTOR_SIZE);
            }
        }

        kfree(tmp);
        nr += run;
    }

    return 0;
}

ssize_t blkcache_write(void *owner, uint64_t sector, size_t count, void *buf,
                       blkcache_io write)
{
    char *in = buf;
    size_t first, n, off;
    ssize_t err = 0;

    if (!count)
        return 0;

    flush_old();
    prefault(buf, count * SECTOR_SIZE, 0);

    uint64_t last = (sector + count - 1) / BLK_SECTORS;

    for (uint64_t nr = sector / BLK_SECTORS; nr <= last; ++nr) {
        overlap(nr, sector, count, &first, &n, &off);

        struct blk *b = lookup(owner, nr);

        if (!b && !(b = block_new(owner, nr))) {
            /* every block is dirty: to the disk, then there is room */
            blkcache_flush();
            b = lookup(owner, nr);

            if (!b && !(b = block_new(owner, nr))) {
                /* no memory: straight to the disk */
                char *tmp = kmalloc(n * SECTOR_SIZE, &M_BLKCACHE, 0);

                if (!tmp)
                    return -ENOMEM;

                memcpy(tmp, in + off * SECTOR_SIZE, n * SECTOR_SIZE);
                err = write(owner, nr * BLK_SECTORS + first, n, tmp);
                kfree(tmp);

                if (err < 0)
                    return err;

                continue;
            }
        }

        uint8_t m = sector_mask(first, n);

        memcpy(blk_data(b) + first * SECTOR_SIZE, in + off * SECTOR_SIZE, n * SECTOR_SIZE);
        b->valid |= m;
        make_dirty(b, m, write);
        touch(b);
    }

    ++writes;

    if (dirty >= DIRTY_MAX)
        err = blkcache_flush();

    return err < 0 ? err : 0;
}
