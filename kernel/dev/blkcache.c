/*
 * Block cache: what disks read and write, kept in 4 KiB blocks for the
 * next reads. Writes go to the disk and then the cache (write through),
 * nothing waits in memory to be written. Once the cache is full, the
 * block used least recently makes room.
 *
 * Drivers use it with absolute sector numbers, after partition offsets,
 * so a sector is in it once whatever device node it was read through.
 * Blocks not cached that a read needs are read with one command.
 *
 * The device's transfers are to and from buffers of the cache's, never
 * the caller's: that may be a process's, which isn't mapped until it is
 * used, and mapping it may read the disk (a driver waiting for itself).
 * The caller's buffer is used once its pages are mapped.
 */

#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <dev/blkcache.h>
#include <aquila/errno.h>

MALLOC_DEFINE(M_BLKCACHE, "blkcache", "block cache");

#define SECTOR_SIZE     512
#define BLK_SECTORS     8
#define BLK_SIZE        (BLK_SECTORS * SECTOR_SIZE)

/* 8 MiB, in one allocation (the kernel heap is a list) */
#define CACHE_BLOCKS    2048
#define CACHE_BUCKETS   1024

/* blocks a read command takes at most */
#define RUN_MAX         16

struct blk {
    void *owner;            /* NULL while the slot is free */
    uint64_t nr;            /* block number, sector / BLK_SECTORS */
    size_t sectors;         /* in it, the last of a disk may have less */

    struct blk *hnext;              /* hash chain */
    struct blk *newer, *older;      /* LRU list */
};

static struct blk blks[CACHE_BLOCKS];
static char *arena;

static struct blk *buckets[CACHE_BUCKETS];
static struct blk *newest, *oldest;
static size_t used;

/* writes so far: a read that waited across one doesn't cache what it read,
 * it may be older than the write */
static uint64_t writes;

static inline char *blk_data(struct blk *b)
{
    return arena + (b - blks) * BLK_SIZE;
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

/* a slot for a block, the oldest one's if all are used */
static struct blk *slot(void)
{
    if (used == CACHE_BLOCKS)
        evict(oldest);

    for (size_t i = 0; i < CACHE_BLOCKS; ++i)
        if (!blks[i].owner)
            return &blks[i];

    return NULL;
}

static void insert(void *owner, uint64_t nr, size_t sectors, const char *data)
{
    if (!arena && !(arena = kmalloc(CACHE_BLOCKS * BLK_SIZE, &M_BLKCACHE, 0)))
        return;

    struct blk *b = lookup(owner, nr);

    if (!b) {
        if (!(b = slot()))
            return;

        b->owner = owner;
        b->nr = nr;
        b->hnext = *bucket(owner, nr);
        *bucket(owner, nr) = b;
        lru_push(b);
        ++used;
    } else {
        touch(b);
    }

    b->sectors = sectors;
    memcpy(blk_data(b), data, sectors * SECTOR_SIZE);
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

ssize_t blkcache_read(void *owner, uint64_t sector, size_t count, void *buf,
                      uint64_t nsectors, blkcache_io read)
{
    char *out = buf;
    uint64_t last = (sector + count - 1) / BLK_SECTORS;
    size_t first, n, off;

    if (!count)
        return 0;

    prefault(buf, count * SECTOR_SIZE, 1);

    for (uint64_t nr = sector / BLK_SECTORS; nr <= last; ) {
        struct blk *b = lookup(owner, nr);

        overlap(nr, sector, count, &first, &n, &off);

        if (b && first + n <= b->sectors) {
            memcpy(out + off * SECTOR_SIZE, blk_data(b) + first * SECTOR_SIZE, n * SECTOR_SIZE);
            touch(b);
            ++nr;
            continue;
        }

        /* the blocks not cached from here, at once */
        size_t run = 1;
        while (nr + run <= last && run < RUN_MAX && !lookup(owner, nr + run))
            ++run;

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

            memcpy(out + off * SECTOR_SIZE, data + first * SECTOR_SIZE, n * SECTOR_SIZE);

            if (w == writes && have)
                insert(owner, nr + i, have, data);
        }

        kfree(tmp);
        nr += run;
    }

    return 0;
}

ssize_t blkcache_write(void *owner, uint64_t sector, size_t count, void *buf,
                       blkcache_io write)
{
    size_t first, n, off;

    if (!count)
        return 0;

    /* the device writes from the cache's copy */
    char *in = kmalloc(count * SECTOR_SIZE, &M_BLKCACHE, 0);

    if (!in)
        return -ENOMEM;

    prefault(buf, count * SECTOR_SIZE, 0);
    memcpy(in, buf, count * SECTOR_SIZE);

    ssize_t err = write(owner, sector, count, in);
    ++writes;

    if (err < 0) {
        kfree(in);
        return err;
    }

    /* the cached copies, of what was written */
    uint64_t last = (sector + count - 1) / BLK_SECTORS;

    for (uint64_t nr = sector / BLK_SECTORS; nr <= last; ++nr) {
        struct blk *b = lookup(owner, nr);

        if (!b)
            continue;

        overlap(nr, sector, count, &first, &n, &off);

        if (first + n <= b->sectors)
            memcpy(blk_data(b) + first * SECTOR_SIZE, in + off * SECTOR_SIZE, n * SECTOR_SIZE);
        else
            evict(b);
    }

    kfree(in);
    return err;
}
