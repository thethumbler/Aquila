#ifndef _DS_HASHMAP_H
#define _DS_HASHMAP_H

#include <core/system.h>

struct hashmap;
struct hashmap_node;

#include <core/string.h>
#include <aquila/errno.h>
#include <ds/queue.h>

typedef uintptr_t hash_t;

#define HASHMAP_DEFAULT 20

/* more entries than this a bucket, on average: twice the buckets */
#define HASHMAP_LOAD    2

/**
 * hashmap
 */
struct hashmap {
    size_t count;

    size_t buckets_nr;
    struct queue *buckets;

    int (*eq)(void *a, void *b);
};

/**
 * hashmap node
 */
struct hashmap_node {
    hash_t hash;
    void *entry;
    struct qnode *qnode;
};

/**
 * iterate over hashmap elements
 */
#define hashmap_for(n, h) \
    for (size_t __i__ = 0; __i__ < (h)->buckets_nr; ++__i__) \
        queue_for ((n), &(h)->buckets[__i__])

/**
 * create a new dynamically allocated hashmap
 */
static inline struct hashmap *hashmap_new(size_t n, int (*eq)(void *, void *))
{
    if (!n) n = HASHMAP_DEFAULT;

    struct hashmap *hashmap;

    hashmap = kmalloc(sizeof(struct hashmap), &M_HASHMAP, 0);
    if (!hashmap) return NULL;


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
 * get the digest of the hash function
 */
static inline hash_t hashmap_digest(const void *_id, size_t size)
{
    /* FNV-1a: every byte changes the whole hash (page offsets, all
     * multiples of 4096, would mostly collide in a sum of the bytes) */
    const unsigned char *id = (const unsigned char *) _id;

#if ARCH_BITS==64
    hash_t hash = 14695981039346656037ULL;
    const hash_t prime = 1099511628211ULL;
#else
    hash_t hash = 2166136261U;
    const hash_t prime = 16777619U;
#endif

    while (size--) {
        hash ^= *id++;
        hash *= prime;
    }

    return hash;
}

/** twice the buckets, the entries move to theirs (the queue nodes are
 * relinked, not made anew); stays as it is if there is no memory */
static inline void hashmap_grow(struct hashmap *hashmap)
{
    size_t nr = hashmap->buckets_nr * 2;
    struct queue *buckets = kmalloc(nr * sizeof(struct queue), &M_QUEUE, M_ZERO);

    if (!buckets)
        return;

    for (size_t i = 0; i < hashmap->buckets_nr; ++i) {
        struct qnode *qnode = hashmap->buckets[i].head;

        while (qnode) {
            struct qnode *next = qnode->next;
            struct hashmap_node *node = (struct hashmap_node *) qnode->value;
            struct queue *queue = &buckets[node->hash % nr];

            qnode->prev = queue->tail;
            qnode->next = NULL;

            if (queue->tail)
                queue->tail->next = qnode;
            else
                queue->head = qnode;

            queue->tail = qnode;
            queue->count++;

            qnode = next;
        }
    }

    kfree(hashmap->buckets);
    hashmap->buckets = buckets;
    hashmap->buckets_nr = nr;
}

/**
 * insert a new element into a hashmap
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

    if (hashmap->count > hashmap->buckets_nr * HASHMAP_LOAD)
        hashmap_grow(hashmap);

    return 0;
}

/**
 * lookup for an element in the hashmap using the hash and the key
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
 * replace an element in the hashmap using the hash and the key
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
 * remove an element from the hashmap given the hashmap node
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

/**
 * free all resources associated with a hashmap
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
