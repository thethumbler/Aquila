#ifndef _DS_QUEUE_H
#define _DS_QUEUE_H

#include <core/system.h>

struct qnode;
struct queue;

#include <core/string.h>

/*
 * Queues: doubly linked lists of pointers, each entry in a node allocated
 * for it. Adding an entry allocates, so it can fail; the kernel's lists
 * that mustn't fail (the scheduler's, the wait queues) are in ds/list.h
 * instead, linked through their entries.
 */

/** A queue's node, holding one entry. */
struct qnode {
    void *value;
    struct qnode *prev;
    struct qnode *next;
};

/* A queue flag: print what is added to it, for debugging. */
#define QUEUE_TRACE 1

/** A queue. */
struct queue {
    size_t count;
    struct qnode *head;
    struct qnode *tail;
    int flags;
};

/* Loops over each node `n` of queue `q`. */
#define queue_for(n, q) for (struct qnode *(n) = (q)->head; (n); (n) = (n)->next)

/* A new, empty, static queue. */
#define QUEUE_NEW() &(struct queue){0}

/** Returns a new, empty queue from kmalloc(), or NULL if there was no
 * memory for it. */
static inline struct queue *queue_new(void)
{
    struct queue *queue;

    queue = kmalloc(sizeof(struct queue), &M_QUEUE, 0);
    if (!queue) return NULL;

    memset(queue, 0, sizeof(struct queue));
    return queue;
}

/** Adds an entry at the end of a queue. Returns its node, or NULL if
 * there was no memory for it. */
static inline struct qnode *enqueue(struct queue *queue, void *value) 
{
    if (!queue)
        return NULL;

    int trace = queue->flags & QUEUE_TRACE;

    if (trace) printk("qtrace: enqueue(%p, %p)\n", queue, value);

    struct qnode *node;
    
    node = kmalloc(sizeof(struct qnode), &M_QNODE, 0);
    if (!node) return NULL;

    memset(node, 0, sizeof(struct qnode));

    if (trace) printk("qtrace: allocated node %p\n", node);

    node->value = value;

    if (!queue->count) {
        /* the queue is empty */
        queue->head = queue->tail = node;
    } else {
        node->prev = queue->tail;
        queue->tail->next = node;
        queue->tail = node;
    }

    ++queue->count;
    return node;
}

/** Adds an entry at the head of a queue, so that dequeue() returns it
 * next. Returns its node, or NULL if there was no memory for it. */
static inline struct qnode *enqueue_head(struct queue *queue, void *value)
{
    if (!queue)
        return NULL;

    struct qnode *node = kmalloc(sizeof(struct qnode), &M_QNODE, 0);
    if (!node) return NULL;

    memset(node, 0, sizeof(struct qnode));
    node->value = value;

    if (!queue->count) {
        queue->head = queue->tail = node;
    } else {
        node->next = queue->head;
        queue->head->prev = node;
        queue->head = node;
    }

    ++queue->count;
    return node;
}

/** Takes the first entry off a queue, frees its node, and returns the
 * entry, or NULL if the queue is empty. */
static inline void *dequeue(struct queue *queue)
{
    if (!queue || !queue->count)
        return NULL;

    --queue->count;
    struct qnode *head = queue->head;

    queue->head = head->next;

    if (queue->head)
        queue->head->prev = NULL;

    if (head == queue->tail)
        queue->tail = NULL;

    void *value = head->value;
    kfree(head);

    return value;
}

/** Removes the first node whose entry is `value`, if there's one. */
static inline void queue_remove(struct queue *queue, void *value)
{
    if (!queue || !queue->count)
        return;

    queue_for (node, queue) {
        if (node->value == value) {
            if (!node->prev) {    /* the head */
                dequeue(queue);
            } else if (!node->next) {   /* the tail */
                --queue->count;
                queue->tail = queue->tail->prev;
                queue->tail->next = NULL;
                kfree(node);
            } else {
                --queue->count;
                node->prev->next = node->next;
                node->next->prev = node->prev;
                kfree(node);
            }

            break;
        }
    }
}

/** Removes a node from a queue, and frees it. */
static inline void queue_node_remove(struct queue *queue, struct qnode *node)
{
    if (!queue || !queue->count || !node)
        return;

    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (queue->head == node)
        queue->head = node->next;
    if (queue->tail == node)
        queue->tail = node->prev;

    --queue->count;
    kfree(node);
    return;
}

#endif /* ! _DS_QUEUE_H */
