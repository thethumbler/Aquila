#ifndef _DS_LIST_H
#define _DS_LIST_H

#include <stddef.h>
#include <core/types.h>

/*
 * A doubly linked list whose links are in the things listed: a structure
 * that goes on a list has a `struct list_node' member for it, and
 * list_entry() gets the structure back from the node. Putting something
 * on a list, or taking it off, allocates nothing, so it can't fail (BSD's
 * TAILQ, Linux's list_head).
 *
 *      struct thread {
 *          ...
 *          struct list_node run_link;  // on the ready list, or a waitq
 *      };
 *
 *      list_append(&ready, &thread->run_link);
 *      struct thread *t = list_entry(list_pop(&ready), struct thread, run_link);
 *
 * A node is on one list at a time; one that's on none has `list' NULL. A
 * structure on several lists has a node for each. A list or a node all
 * zeros is empty, not on a list: {0} initializes them.
 */

struct list;

struct list_node {
    struct list_node *prev, *next;
    struct list *list;          /* the list it's on, NULL if none */
};

struct list {
    struct list_node *head, *tail;
    size_t count;
};

/* the structure of `type' whose member `member' is the node `node' */
#define list_entry(node, type, member) \
    ((type *) ((char *) (node) - offsetof(type, member)))

/* each node of `list', `n' the node (it may be removed meanwhile: the
 * next one is taken before) */
#define list_for(n, l) \
    for (struct list_node *(n) = (l)->head, *__next_##n = (n) ? (n)->next : NULL; \
         (n); (n) = __next_##n, __next_##n = (n) ? (n)->next : NULL)

static inline int list_empty(struct list *list)
{
    return !list->head;
}

/** whether `node' is on a list (which, node->list says) */
static inline int list_linked(struct list_node *node)
{
    return node->list != NULL;
}

/** `node' at the end of `list' */
static inline void list_append(struct list *list, struct list_node *node)
{
    node->list = list;
    node->next = NULL;
    node->prev = list->tail;

    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;

    list->tail = node;
    list->count++;
}

/** `node' at the head of `list', the next list_pop() */
static inline void list_prepend(struct list *list, struct list_node *node)
{
    node->list = list;
    node->prev = NULL;
    node->next = list->head;

    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;

    list->head = node;
    list->count++;
}

/** `node' off the list it's on (none: nothing) */
static inline void list_remove(struct list_node *node)
{
    struct list *list = node->list;

    if (!list)
        return;

    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    node->prev = node->next = NULL;
    node->list = NULL;
    list->count--;
}

/** the head of `list', off it; NULL if it's empty */
static inline struct list_node *list_pop(struct list *list)
{
    struct list_node *node = list->head;

    if (node)
        list_remove(node);

    return node;
}

#endif /* ! _DS_LIST_H */
