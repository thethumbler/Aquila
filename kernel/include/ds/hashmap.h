#ifndef _DS_HASHMAP_H
#define _DS_HASHMAP_H

#include <core/system.h>

struct hashmap;
struct hashmap_node;

#include <core/string.h>
#include <aquila/errno.h>
#include <ds/queue.h>

/*
 * Hash tables: entries found by a hash of their key. Each bucket is a
 * queue of nodes, and a node points to its entry. The caller computes
 * the hash (hashmap_digest()), and gives the table a function that says
 * whether an entry is the one for a key. The table doubles its buckets
 * when it holds more than HASHMAP_LOAD entries per bucket on average.
 */

/* A key's hash. */
typedef uintptr_t hash_t;

/* How many buckets a new table has, unless the caller says. */
#define HASHMAP_DEFAULT 20

/* With more entries than this per bucket on average, the table doubles
 * its buckets. */
#define HASHMAP_LOAD    2

/** A hash table. */
struct hashmap {
    size_t count;

    size_t buckets_nr;
    struct queue *buckets;

    /* says whether entry `a` is the one for key `b` */
    int (*eq)(void *a, void *b);
};

/** A hash table's node: an entry, with its key's hash. */
struct hashmap_node {
    hash_t hash;
    void *entry;
    struct qnode *qnode;
};

/* Loops over each node of a hash table, as a queue node `n` whose value
 * is the hashmap_node. */
#define hashmap_for(n, h) \
    for (size_t __i__ = 0; __i__ < (h)->buckets_nr; ++__i__) \
        queue_for ((n), &(h)->buckets[__i__])

/** Makes a new, empty hash table of `n` buckets (0: HASHMAP_DEFAULT).
 * Returns it, or NULL if there was no memory for it. */
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

/** Returns the hash of a key of `size` bytes. */
static inline hash_t hashmap_digest(const void *_id, size_t size)
{
    /* FNV-1a, where every byte changes the whole hash: page offsets, all
     * multiples of 4096, would mostly collide in a sum of the bytes */
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

/** Doubles a table's buckets, and moves each entry to its new bucket.
 * The queue nodes are relinked rather than made anew. If there's no
 * memory for the new buckets, the table stays as it is. */
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

/** Adds an entry to a hash table. Returns 0, `-EINVAL` for a bad table,
 * or `-ENOMEM` if there was no memory for its node. */
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

/** Finds the node of the entry for a key, or returns NULL if there's
 * none. */
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

/** Replaces the entry for a key, or adds it if there's none. Returns 0,
 * or what hashmap_insert() returned. */
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

/** Removes a node from a hash table, and frees it (not its entry). */
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

/** Frees a hash table and its nodes (not their entries). */
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

#endif /* ! _DS_HASHMAP_H */
