/** \file
 * The block cache: a filesystem's blocks kept in memory, by a number the
 * filesystem chooses (minix uses the block number), so that a block read
 * again and again is read once. minix keeps its indirect zones there.
 */

#include <core/system.h>
#include <fs/bcache.h>

MALLOC_DEFINE(M_BCACHE, "bcache", "block cache structure");
MALLOC_DEFINE(M_CACHE_BLOCK, "cache block", "cached block structure");

/** A cached block. */
struct cache_block {
    off_t off;      /* its number */
    void *data;     /* its contents */

    int dirty;      /* changed since it was read (not written back yet) */
};

/** Says whether a cached block is the one whose number `_b` points to. */
static int bcache_eq(void *_a, void *_b)
{
    struct cache_block *a = (struct cache_block *) _a;
    off_t *b = (off_t *) _b;

    return a->off == *b;
}

/**
 * Sets up an empty block cache.
 *
 * @param bcache  the cache
 */
void bcache_init(struct bcache *bcache)
{
    bcache->hashmap = hashmap_new(0, bcache_eq);
}

/**
 * Adds a block to a cache, which keeps its memory from now on.
 *
 * @param bcache  the cache, set up on first use
 * @param off     the block's number
 * @param data    its contents
 * @return 0, `-EINVAL` if `bcache` is NULL, or `-ENOMEM`
 */
int bcache_insert(struct bcache *bcache, uint64_t off, void *data)
{
    if (!bcache)
        return -EINVAL;

    if (!bcache->hashmap)
        bcache_init(bcache);

    struct cache_block *block = kmalloc(sizeof(struct cache_block), &M_CACHE_BLOCK, M_ZERO);
    if (!block) return -ENOMEM;

    block->off = off;
    block->data = data;

    hash_t hash = hashmap_digest(&off, sizeof(off));
    return hashmap_insert(bcache->hashmap, hash, block);
}

/**
 * Takes a block out of a cache.
 *
 * @param bcache  the cache
 * @param off     the block's number
 * @return 0, or -1 if the block wasn't cached
 */
int bcache_remove(struct bcache *bcache, uint64_t off)
{
    if (!bcache || !bcache->hashmap)
        return -1;

    hash_t hash = hashmap_digest(&off, sizeof(off));

    struct hashmap_node *node = hashmap_lookup(bcache->hashmap, hash, &off);

    if (node) {
        void *block = node->entry;
        node->entry = NULL;
        kfree(block);
        hashmap_node_remove(bcache->hashmap, node);
        return 0;
    }

    return -1;
}

/**
 * Finds a cached block.
 *
 * @param bcache  the cache
 * @param off     the block's number
 * @return its contents, or NULL if it isn't cached
 */
void *bcache_find(struct bcache *bcache, uint64_t off)
{
    if (!bcache || !bcache->hashmap)
        return NULL;

    hash_t hash = hashmap_digest(&off, sizeof(off));

    struct hashmap_node *node = hashmap_lookup(bcache->hashmap, hash, &off);

    if (node) {
        struct cache_block *block = (struct cache_block *) node->entry;
        return block->data;
    }

    return NULL;
}

/**
 * Marks a cached block as changed. Nothing writes dirty blocks back yet:
 * the filesystems write their changes through at once.
 *
 * @param bcache  the cache
 * @param off     the block's number
 */
void bcache_dirty(struct bcache *bcache, uint64_t off)
{
    if (!bcache || !bcache->hashmap)
        return;

    hash_t hash = hashmap_digest(&off, sizeof(off));

    struct hashmap_node *node = hashmap_lookup(bcache->hashmap, hash, &off);

    if (node) {
        struct cache_block *block = (struct cache_block *) node->entry;
        block->dirty = 1;
    }
}
