/*
 * Callouts (sys/callout.h): functions called at a time to come, from the
 * timer's interrupt (sched_timer_tick), which checks them on each tick
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/callout.h>

static struct callout *callouts;

void callout_stop(struct callout *c)
{
    if (!c->pending)
        return;

    for (struct callout **p = &callouts; *p; p = &(*p)->next) {
        if (*p == c) {
            *p = c->next;
            break;
        }
    }

    c->pending = 0;
}

void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg)
{
    callout_stop(c);

    c->when = arch_rtime_ns() + ns;
    c->fn   = fn;
    c->arg  = arg;
    c->pending = 1;

    c->next = callouts;
    callouts = c;
}

void callout_run(uint64_t now)
{
    struct callout **p = &callouts;

    while (*p) {
        struct callout *c = *p;

        if (now < c->when) {
            p = &c->next;
            continue;
        }

        /* off the list before it is called, it may set itself again */
        *p = c->next;
        c->pending = 0;
        c->fn(c->arg);

        /* the list may have changed: from the start */
        p = &callouts;
    }
}
