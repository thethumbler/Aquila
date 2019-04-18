#ifndef _QUEUE_H
#define _QUEUE_H

#include <core/system.h>

struct qnode;
struct queue;

#include <core/string.h>

struct qnode {
    void *value;
    struct qnode *prev;
    struct qnode *next;
};

#define QUEUE_TRACE 1

struct queue {
    size_t count;
    struct qnode *head;
    struct qnode *tail;
    int flags;
};

#define QUEUE_NEW() &(struct queue){0}
static inline void *queue_new(void)
{
    struct queue *queue;

    queue = kmalloc(sizeof(struct queue), &M_QUEUE, 0);
    if (!queue) return NULL;

    memset(queue, 0, sizeof(struct queue));
    return queue;
}

static inline struct qnode *enqueue(struct queue *queue, void *value) 
{
    if (!queue)
        return NULL;

    int trace = queue->flags & QUEUE_TRACE;

    if (trace) printk("qtrace: enqueue(%p, %p)\n", queue, value);

    struct qnode *node;
    
    node = kmalloc(sizeof(struct qnode), &M_QNODE, 0);
    if (!node) return NULL;

    if (trace) printk("qtrace: allocated node %p\n", node);

    node->value = value;
    node->next  = NULL;
    node->prev  = NULL;

    if (!queue->count) {
        /* Queue is not initalized */
        queue->head = queue->tail = node;
    } else {
        node->prev = queue->tail;
        queue->tail->next = node;
        queue->tail = node;
    }

    ++queue->count;
    return node;
}

static inline void *dequeue(struct queue *queue)
{
    if (!queue || !queue->count)
        return NULL;

    --queue->count;
    struct qnode *head = queue->head;

    queue->head = head->next;

    if (queue->head)
        queue->head->prev = NULL;
    void *value = head->value;
    kfree(head);

    return value;
}

static inline void queue_remove(struct queue *queue, void *value)
{
    if (!queue || !queue->count)
        return;

    forlinked (node, queue->head, node->next) {
        if (node->value == value) {
            if (!node->prev) {    /* Head */
                dequeue(queue);
            } else if (!node->next) {   /* Tail */
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

#endif /* _QUEUE_H */
