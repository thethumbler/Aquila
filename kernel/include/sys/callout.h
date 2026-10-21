#ifndef _SYS_CALLOUT_H
#define _SYS_CALLOUT_H

#include <core/system.h>

/**
 * \ingroup sys
 * \brief a function called later (from the timer's interrupt), the
 * caller's structure: arming it doesn't allocate
 */
struct callout {
    uint64_t when;              /* arch_rtime_ns() */
    void (*fn)(void *arg);
    void *arg;
    struct callout *next;
    int pending;
};

/* fn(arg) in `ns' nanoseconds (again, if it was armed: it moves) */
void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg);

/* not called, if it wasn't yet */
void callout_stop(struct callout *c);

/* the timer's tick: those due are called */
void callout_run(uint64_t now);

#endif /* ! _SYS_CALLOUT_H */
