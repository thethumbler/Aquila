#include <core/system.h>
#include <fs/icache.h>

MALLOC_DEFINE(M_ICACHE, "icache", "inode cache structure");

static int icache_eq(void *_a, void *_b)
{
    struct inode *a = (struct inode *) _a;
    ino_t *b = (ino_t *) _b;

    return a->ino == *b;
}

/** initialize inode caching structure
 * \ingroup vfs
 */
void icache_init(struct icache *icache)
{
    icache->hashmap = hashmap_new(0, icache_eq);
}

int icache_insert(struct icache *icache, struct inode *inode)
{
    if (!icache)
        return -EINVAL;

    if (!icache->hashmap)
        icache_init(icache);

    hash_t hash = hashmap_digest(&inode->ino, sizeof(inode->ino));
    return hashmap_insert(icache->hashmap, hash, inode);
}

int icache_remove(struct icache *icache, struct inode *inode)
{
    if (!icache || !icache->hashmap)
        return -1;

    hash_t hash = hashmap_digest(&inode->ino, sizeof(ino_t));

    struct hashmap_node *node = hashmap_lookup(icache->hashmap, hash, &inode->ino);

    if (node) {
        hashmap_node_remove(icache->hashmap, node);
        return 0;
    }

    return -1;
}

struct inode *icache_find(struct icache *icache, ino_t ino)
{
    if (!icache || !icache->hashmap)
        return NULL;

    hash_t hash = hashmap_digest(&ino, sizeof(ino));

    struct hashmap_node *node = hashmap_lookup(icache->hashmap, hash, &ino);

    if (node) {
        return (struct inode *) node->entry;
    }

    return NULL;
}
