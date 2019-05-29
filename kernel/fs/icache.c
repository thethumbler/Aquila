#include <core/system.h>
#include <fs/icache.h>

MALLOC_DEFINE(M_ICACHE, "icache", "inode cache structure");

void icache_init(struct icache *icache)
{
    icache->hashmap = hashmap_new(0);
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
        return -EINVAL;

    hash_t hash = hashmap_digest(&inode->ino, sizeof(inode->ino));
    hashmap_remove(icache->hashmap, hash);

    return 0;
}

struct inode *icache_find(struct icache *icache, ino_t ino)
{
    if (!icache || !icache->hashmap)
        return NULL;

    hash_t hash = hashmap_digest(&ino, sizeof(ino));

    struct hashmap_node *node = hashmap_lookup(icache->hashmap, hash);

    if (node) {
        return (struct inode *) node->entry;
    }

    return NULL;
}
