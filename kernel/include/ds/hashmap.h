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

struct hashmap {
    size_t count;

    size_t buckets_nr;
    struct queue *buckets;
};

struct hashmap_node {
    hash_t id;
    void *entry;
    struct qnode *qnode;
};

#define HASHMAP_NEW(n) &(struct hashmap) {\
    .count = 0, \
    .buckets_nr = HASHMAP_DEFAULT, \
    .buckets = &(struct queue[n]){0}, \
}

static inline struct hashmap *hashmap_new(size_t n)
{
    if (!n) n = HASHMAP_DEFAULT;

    struct hashmap *hashmap;

    hashmap = kmalloc(sizeof(struct hashmap), &M_HASHMAP, 0);
    if (!hashmap) return NULL;

    memset(hashmap, 0, sizeof(struct hashmap));

    hashmap->buckets = kmalloc(n * sizeof(struct queue), &M_QUEUE, 0);

    if (!hashmap->buckets) {
        kfree(hashmap);
        return NULL;
    }

    memset(hashmap->buckets, 0, n * sizeof(struct queue));

    hashmap->buckets_nr = n;

    return hashmap;
}

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

static inline int hashmap_insert(struct hashmap *hashmap, hash_t id, void *entry) 
{
    if (!hashmap || !hashmap->buckets_nr || !hashmap->buckets)
        return -EINVAL;

    struct hashmap_node *node;
    
    node = kmalloc(sizeof(struct hashmap_node), &M_HASHMAP_NODE, 0);

    if (!node) return -ENOMEM;

    node->id    = id;
    node->entry = entry;

    size_t idx = id % hashmap->buckets_nr;

    node->qnode = enqueue(&hashmap->buckets[idx], node);

    if (!node->qnode) {
        kfree(node);
        return -ENOMEM;
    }

    hashmap->count++;

    return 0;
}

static inline struct hashmap_node *hashmap_lookup(struct hashmap *hashmap, hash_t id)
{
    if (!hashmap || !hashmap->buckets || !hashmap->buckets_nr || !hashmap->count)
        return NULL;

    size_t idx = id % hashmap->buckets_nr;
    struct queue *queue = &hashmap->buckets[idx];

    for (struct qnode *node = queue->head; node; node = node->next) {
        struct hashmap_node *hnode = (struct hashmap_node *) node->value;
        if (hnode && hnode->id == id)
            return hnode;
    }

    return NULL;
}

static inline void hashmap_node_remove(struct hashmap *hashmap, struct hashmap_node *node)
{
    if (!hashmap || !hashmap->buckets ||
            !hashmap->buckets_nr || !node || !node->qnode)
        return;

    size_t idx = node->id % hashmap->buckets_nr;
    struct queue *queue = &hashmap->buckets[idx];

    queue_node_remove(queue, node->qnode);
    hashmap->count--;
}

static inline void hashmap_remove(struct hashmap *hashmap, hash_t id)
{
    struct hashmap_node *node = hashmap_lookup(hashmap, id);

    if (node)
        hashmap_node_remove(hashmap, node);
}

#endif /* ! _DS_QUEUE_H */
