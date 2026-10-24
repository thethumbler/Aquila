/** \file
 * The vnode cache: a filesystem's vnodes that are in use, by inode
 * number, so that a file opened twice gets the same vnode.
 */

#include <core/system.h>
#include <fs/vcache.h>

MALLOC_DEFINE(M_VCACHE, "vcache", "vnode cache structure");

/** Says whether a cached vnode has the inode number `_b` points to. */
static int vcache_eq(void *_a, void *_b)
{
    struct vnode *a = (struct vnode *) _a;
    ino_t *b = (ino_t *) _b;

    return a->ino == *b;
}

/**
 * Sets up an empty vnode cache.
 *
 * @param vcache  the cache
 */
void vcache_init(struct vcache *vcache)
{
    vcache->hashmap = hashmap_new(0, vcache_eq);
}

/**
 * Adds a vnode to a cache.
 *
 * @param vcache  the cache, set up on first use
 * @param vnode   the vnode
 * @return 0, `-EINVAL` if `vcache` is NULL, or `-ENOMEM`
 */
int vcache_insert(struct vcache *vcache, struct vnode *vnode)
{
    if (!vcache)
        return -EINVAL;

    if (!vcache->hashmap)
        vcache_init(vcache);

    hash_t hash = hashmap_digest(&vnode->ino, sizeof(vnode->ino));
    return hashmap_insert(vcache->hashmap, hash, vnode);
}

/**
 * Takes a vnode out of a cache.
 *
 * @param vcache  the cache
 * @param vnode   the vnode
 * @return 0, or -1 if the vnode wasn't in the cache
 */
int vcache_remove(struct vcache *vcache, struct vnode *vnode)
{
    if (!vcache || !vcache->hashmap)
        return -1;

    hash_t hash = hashmap_digest(&vnode->ino, sizeof(ino_t));

    struct hashmap_node *node = hashmap_lookup(vcache->hashmap, hash, &vnode->ino);

    if (node) {
        hashmap_node_remove(vcache->hashmap, node);
        return 0;
    }

    return -1;
}

/**
 * Finds the cached vnode of an inode.
 *
 * @param vcache  the cache
 * @param ino     the inode number
 * @return the vnode, or NULL if it isn't cached
 */
struct vnode *vcache_find(struct vcache *vcache, ino_t ino)
{
    if (!vcache || !vcache->hashmap)
        return NULL;

    hash_t hash = hashmap_digest(&ino, sizeof(ino));

    struct hashmap_node *node = hashmap_lookup(vcache->hashmap, hash, &ino);

    if (node) {
        return (struct vnode *) node->entry;
    }

    return NULL;
}
