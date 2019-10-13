#include <core/system.h>
#include <fs/bcache.h>

MALLOC_DEFINE(M_BCACHE, "bcache", "block cache structure");
MALLOC_DEFINE(M_CACHE_BLOCK, "cache block", "cached block structure");

/**
 * \ingroup vfs
 * \brief cacehed block
 */
struct cache_block {
    off_t off;
    void *data;

    int dirty;
};

static int bcache_eq(void *_a, void *_b)
{
    struct cache_block *a = (struct cache_block *) _a;
    off_t *b = (off_t *) _b;

    return a->off == *b;
}

void bcache_init(struct bcache *bcache)
{
    bcache->hashmap = hashmap_new(0, bcache_eq);
}

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

int bcache_remove(struct bcache *bcache, uint64_t off)
{
    if (!bcache || !bcache->hashmap)
        return -1;

    hash_t hash = hashmap_digest(&off, sizeof(off));

    struct hashmap_node *node = hashmap_lookup(bcache->hashmap, hash, &off);

    if (node) {
        void *block = node->entry;
        node->entry = NULL;
        kfree(node->entry);
        hashmap_node_remove(bcache->hashmap, node);
        return 0;
    }

    return -1;
}

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
