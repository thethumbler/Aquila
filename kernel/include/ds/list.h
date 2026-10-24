#ifndef _DS_LIST_H
#define _DS_LIST_H

#include <stddef.h>
#include <core/types.h>

/*
 * A doubly linked list whose links are inside the things it lists. A
 * structure that can be on a list has a `struct list_node` member for it,
 * and list_entry() gets the structure back from its node. Putting
 * something on a list, or taking it off, allocates nothing, so it can't
 * fail. BSD's TAILQ and Linux's list_head work the same way.
 *
 *      struct thread {
 *          ...
 *          struct list_node run_link;  // on the ready list, or a waitq
 *      };
 *
 *      list_append(&ready, &thread->run_link);
 *      struct thread *t = list_entry(list_pop(&ready), struct thread, run_link);
 *
 * A node is on at most one list at a time, and its `list` field says
 * which one (NULL when it's on none). A structure that can be on several
 * lists at once has one node for each. A list or a node that is all
 * zeros is empty, or on no list, so `{0}` initializes both.
 */

struct list;

/** A link in a list, inside the structure that is listed. */
struct list_node {
    struct list_node *prev, *next;
    struct list *list;          /* the list it's on, or NULL */
};

/** A list: its first and last nodes, and how many there are. */
struct list {
    struct list_node *head, *tail;
    size_t count;
};

/* The structure of type `type` whose member `member` is the node `node`. */
#define list_entry(node, type, member) \
    ((type *) ((char *) (node) - offsetof(type, member)))

/* Loops over each node `n` of list `l`. The loop body may remove `n`
 * from the list: the next node is taken before the body runs. */
#define list_for(n, l) \
    for (struct list_node *(n) = (l)->head, *__next_##n = (n) ? (n)->next : NULL; \
         (n); (n) = __next_##n, __next_##n = (n) ? (n)->next : NULL)

/** Returns whether `list` has no nodes. */
static inline int list_empty(struct list *list)
{
    return !list->head;
}

/** Returns whether `node` is on a list (`node->list` says which). */
static inline int list_linked(struct list_node *node)
{
    return node->list != NULL;
}

/** Adds `node` at the end of `list`. */
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

/** Adds `node` at the head of `list`, so that list_pop() returns it next. */
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

/** Takes `node` off the list it is on. A node on no list is left alone. */
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

/** Takes the first node off `list` and returns it, or NULL if it's empty. */
static inline struct list_node *list_pop(struct list *list)
{
    struct list_node *node = list->head;

    if (node)
        list_remove(node);

    return node;
}

#endif /* ! _DS_LIST_H */
