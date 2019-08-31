#ifndef _DS_HASHMAP_H
#define _DS_HASHMAP_H

#include <core/system.h>

struct hashmap;
struct hashmap_node;

#include <core/string.h>
#include <bits/errno.h>
#include <ds/queue.h>

typedef uintptr_t hash_t;

#define HASHMAP_DEFAULT 20

/**
 * \ingroup ds
 * \brief hashmap
 */
struct hashmap {
    size_t count;

    size_t buckets_nr;
    struct queue *buckets;

    int (*eq)(void *a, void *b);
};

/**
 * \ingroup ds
 * \brief hashmap node
 */
struct hashmap_node {
    hash_t hash;
    void *entry;
    struct qnode *qnode;
};

/**
 * \ingroup ds
 * \brief iterate over hashmap elements
 */
#define hashmap_for(n, h) \
    for (size_t __i__ = 0; __i__ < (h)->buckets_nr; ++__i__) \
        queue_for ((n), &(h)->buckets[__i__])

/**
 * \ingroup ds
 * \brief create a new statically allocated hashmap
 */
#define HASHMAP_NEW(n) &(struct hashmap) {\
    .count = 0, \
    .buckets_nr = HASHMAP_DEFAULT, \
    .buckets = &(struct queue[n]){0}, \
}

/**
 * \ingroup ds
 * \brief create a new dynamically allocated hashmap
 */
static inline struct hashmap *hashmap_new(size_t n, int (*eq)(void *, void *))
{
    if (!n) n = HASHMAP_DEFAULT;

    struct hashmap *hashmap;

    hashmap = kmalloc(sizeof(struct hashmap), &M_HASHMAP, 0);
    if (!hashmap) return NULL;

    //printk("hashmap_new(%d) -> %p\n", n, hashmap);

    memset(hashmap, 0, sizeof(struct hashmap));

    hashmap->eq = eq;
    hashmap->buckets = kmalloc(n * sizeof(struct queue), &M_QUEUE, 0);

    if (!hashmap->buckets) {
        kfree(hashmap);
        return NULL;
    }

    memset(hashmap->buckets, 0, n * sizeof(struct queue));

    hashmap->buckets_nr = n;

    return hashmap;
}

/**
 * \ingroup ds
 * \brief get the digest of the hash function
 */
static inline hash_t hashmap_digest(const void *_id, size_t size)
{
    const char *id = (const char *) _id;

    hash_t hash = 0;

    while (size) {
        hash += *id;
        ++id;
        --size;
    }

    return hash;
}

/**
 * \ingroup ds
 * \brief insert a new element into a hashmap
 */
static inline int hashmap_insert(struct hashmap *hashmap, hash_t hash, void *entry) 
{
    if (!hashmap || !hashmap->buckets_nr || !hashmap->buckets)
        return -EINVAL;

    struct hashmap_node *node;
    
    node = kmalloc(sizeof(struct hashmap_node), &M_HASHMAP_NODE, 0);

    if (!node) return -ENOMEM;

    node->hash  = hash;
    node->entry = entry;

    size_t idx = hash % hashmap->buckets_nr;

    node->qnode = enqueue(&hashmap->buckets[idx], node);

    if (!node->qnode) {
        kfree(node);
        return -ENOMEM;
    }

    hashmap->count++;

    return 0;
}

/**
 * \ingroup ds
 * \brief lookup for an element in the hashmap using the hash and the key
 */
static inline struct hashmap_node *hashmap_lookup(struct hashmap *hashmap, hash_t hash, void *key)
{
    if (!hashmap || !hashmap->buckets || !hashmap->buckets_nr || !hashmap->count)
        return NULL;

    size_t idx = hash % hashmap->buckets_nr;
    struct queue *queue = &hashmap->buckets[idx];

    queue_for (node, queue) {
        struct hashmap_node *hnode = (struct hashmap_node *) node->value;
        if (hnode && hnode->hash == hash && hashmap->eq(hnode->entry, key))
            return hnode;
    }

    return NULL;
}

/**
 * \ingroup ds
 * \brief replace an element in the hashmap using the hash and the key
 */
static inline int hashmap_replace(struct hashmap *hashmap, hash_t hash, void *key, void *entry)
{
    if (!hashmap || !hashmap->buckets || !hashmap->buckets_nr || !hashmap->count)
        return -EINVAL;

    size_t idx = hash % hashmap->buckets_nr;
    struct queue *queue = &hashmap->buckets[idx];

    queue_for (node, queue) {
        struct hashmap_node *hnode = (struct hashmap_node *) node->value;
        if (hnode && hnode->hash == hash && hashmap->eq(hnode->entry, key)) {
            hnode->entry = entry;
            return 0;
        }
    }

    return hashmap_insert(hashmap, hash, entry);
}

/**
 * \ingroup ds
 * \brief remove an element from the hashmap given the hashmap node
 */
static inline void hashmap_node_remove(struct hashmap *hashmap, struct hashmap_node *node)
{
    if (!hashmap || !hashmap->buckets ||
        !hashmap->buckets_nr || !node || !node->qnode)
        return;

    size_t idx = node->hash % hashmap->buckets_nr;
    struct queue *queue = &hashmap->buckets[idx];

    queue_node_remove(queue, node->qnode);
    kfree(node);
    hashmap->count--;
}

#if 0
static inline void hashmap_remove(struct hashmap *hashmap, hash_t id)
{
    struct hashmap_node *node = hashmap_lookup(hashmap, id);

    if (node)
        hashmap_node_remove(hashmap, node);
}
#endif

/**
 * \ingroup ds
 * \brief free all resources associated with a hashmap
 */
static inline void hashmap_free(struct hashmap *hashmap)
{
    for (size_t i = 0; i < hashmap->buckets_nr; ++i) {
        struct queue *queue = &hashmap->buckets[i];

        struct hashmap_node *node;
        while ((node = dequeue(queue))) {
            kfree(node);
        }
    }

    kfree(hashmap->buckets);
    kfree(hashmap);
}

#endif /* ! _DS_QUEUE_H */
