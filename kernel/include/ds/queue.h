#ifndef _QUEUE_H
#define _QUEUE_H

#include <core/system.h>
#include <core/string.h>
#include <core/printk.h>
#include <mm/mm.h>

typedef struct queue queue_t;
struct queue_node {
    void *value;
    struct queue_node *prev;
    struct queue_node *next;
} __packed;

struct queue {
    size_t count;
    struct queue_node *head;
    struct queue_node *tail;
} __packed;

static inline struct queue_node *enqueue(queue_t *queue, void *value) 
{
    struct queue_node *node = kmalloc(sizeof(struct queue_node));

    if (!node)
        return NULL;

    node->value = value;
    node->next = NULL;
    node->prev = NULL;

    if (!queue->count) {    /* Queue is not initalized */
        queue->head = queue->tail = node;
    } else {
        node->prev = queue->tail;
        queue->tail->next = node;
        queue->tail = node;
    }

    ++queue->count;

    return node;
}

static inline void *dequeue(queue_t *queue)
{
    if (!queue->count)  /* Queue is empty! */
        return NULL;

    --queue->count;
    struct queue_node *head = queue->head;

    queue->head = head->next;
    if (queue->head)
        queue->head->prev = NULL;
    void *value = head->value;
    kfree(head);
    return value;
}

static inline void queue_remove(queue_t *queue, void *value)
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

static inline void queue_node_remove(queue_t *queue, struct queue_node *node)
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

static inline void *queue_new()
{
    queue_t *q = kmalloc(sizeof(queue_t));

    if (!q)
        return NULL;

    memset(q, 0, sizeof(queue_t));
    return q;
}

#define QUEUE_NEW() &(struct queue){0}

#if 0
static inline void *queue_find(queue_t *queue, int (*check) (void *))
{
    forlinked(node, queue->head, node->next)
        if(check(node->value))
            return node->value;

    return NULL;
}

static inline void queue_traverse(queue_t *queue, void (*func) (void *))
{
    forlinked(node, queue->head, node->next)
        func(node->value);
}
#endif

#endif /* _QUEUE_H */
