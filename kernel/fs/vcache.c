#include <core/system.h>
#include <fs/vcache.h>

MALLOC_DEFINE(M_VCACHE, "vcache", "vnode cache structure");

static int vcache_eq(void *_a, void *_b)
{
    struct vnode *a = (struct vnode *) _a;
    ino_t *b = (ino_t *) _b;

    return a->ino == *b;
}

/**
 * \ingroup vfs
 * \brief initialize vnode caching structure
 */
void vcache_init(struct vcache *vcache)
{
    vcache->hashmap = hashmap_new(0, vcache_eq);
}

int vcache_insert(struct vcache *vcache, struct vnode *vnode)
{
    if (!vcache)
        return -EINVAL;

    if (!vcache->hashmap)
        vcache_init(vcache);

    hash_t hash = hashmap_digest(&vnode->ino, sizeof(vnode->ino));
    return hashmap_insert(vcache->hashmap, hash, vnode);
}

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
