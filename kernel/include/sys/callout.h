#ifndef _SYS_CALLOUT_H
#define _SYS_CALLOUT_H

#include <core/system.h>

/**
 * \ingroup sys
 * \brief a function called later, the caller's structure: arming it
 * doesn't allocate
 */
struct callout {
    uint64_t when;              /* arch_rtime_ns() */
    void (*fn)(void *arg);
    void *arg;
    struct callout *next;
    struct callout_list *list;  /* the one it's on, while pending */
    int pending;
};

/**
 * \ingroup sys
 * \brief callouts called from one context: the timer's interrupt
 * (callout_set), or a thread that runs its own (the network's)
 */
struct callout_list {
    struct callout *head;
};

/* fn(arg) in `ns' nanoseconds (again, if it was armed: it moves), from
 * the timer's interrupt */
void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg);

/* the same, from whoever runs `list' (callout_list_run) */
void callout_set_on(struct callout_list *list, struct callout *c, uint64_t ns,
                    void (*fn)(void *), void *arg);

/* not called, if it wasn't yet */
void callout_stop(struct callout *c);

/* those of `list' due at `now' are called */
void callout_list_run(struct callout_list *list, uint64_t now);

/* when the first of `list' is due, 0 if none is */
uint64_t callout_list_next(struct callout_list *list);

/* the timer's tick: its callouts due are called */
void callout_run(uint64_t now);

#endif /* ! _SYS_CALLOUT_H */
