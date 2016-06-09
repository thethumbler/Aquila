#ifndef _QUEUE_H
#define _QUEUE_H

#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>

typedef struct queue queue_t;
struct queue_node
{
	void *value;
	struct queue_node *next;
} __packed;

struct queue
{
	size_t count;
	struct queue_node *head;
	struct queue_node *tail;
} __packed;

static inline void enqueue(queue_t *queue, void *value) 
{
	struct queue_node *node = kmalloc(sizeof(struct queue_node));
	node->value = value;
	node->next = NULL;

	if(!queue->count)	/* Queue is not initalized */
	{
		queue->head = queue->tail = node;
	} else
	{
		queue->tail->next = node;
		queue->tail = node;
	}

	++queue->count;
}

static inline void *dequeue(queue_t *queue)
{
	if(!queue->count)	/* Queue is empty! */
		return NULL;

	--queue->count;
	struct queue_node *head = queue->head;
	queue->head = queue->head->next;
	void *value = head->value;
	kfree(head);
	return value;
}

static inline void *new_queue()
{
	return memset(kmalloc(sizeof(queue_t)), 0, sizeof(queue_t));
}

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