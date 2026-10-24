#ifndef _SYS_CALLOUT_H
#define _SYS_CALLOUT_H

#include <core/system.h>

/*
 * Callouts (sys/callout.c): functions called later, at a given time. The
 * caller owns the structure, so arming one allocates nothing and can't
 * fail.
 */

/** A function to call later, with its argument. */
struct callout {
    uint64_t when;              /* when to call it, in arch_rtime_ns() time */
    void (*fn)(void *arg);
    void *arg;
    struct callout *next;
    struct callout_list *list;  /* the list it's on, while it's pending */
    int pending;
};

/** A list of callouts called from one context: the timer's interrupt
 * (callout_set()), or a thread that runs its own list (the network's). */
struct callout_list {
    struct callout *head;
};

/* Calls fn(arg) in `ns` nanoseconds, from the timer's interrupt. A
 * callout that was armed already moves to the new time. */
void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg);

/* The same, but called from whoever runs `list` (callout_list_run()). */
void callout_set_on(struct callout_list *list, struct callout *c, uint64_t ns,
                    void (*fn)(void *), void *arg);

/* Disarms a callout, if it wasn't called yet. */
void callout_stop(struct callout *c);

/* Calls the callouts of `list` that are due at `now`. */
void callout_list_run(struct callout_list *list, uint64_t now);

/* Returns when the first callout of `list` is due, or 0 if none is
 * armed. */
uint64_t callout_list_next(struct callout_list *list);

/* Calls the timer's callouts that are due, on each tick. */
void callout_run(uint64_t now);

#endif /* ! _SYS_CALLOUT_H */
