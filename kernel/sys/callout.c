/** \file
 * Callouts: functions to call at a time to come (see `sys/callout.h`).
 *
 * Each list of callouts is run from one place only. The timer's list is
 * run by the timer interrupt (`sched_timer_tick()`), which checks it on
 * every tick. Any other list is run by the thread that owns it, which
 * sleeps until the first callout is due (`callout_list_next()`): TCP's
 * timers run in the network's thread this way. The caller owns each
 * `struct callout`, so setting one never allocates and never fails.
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/callout.h>

static struct callout_list timer_callouts;

/**
 * Cancels a callout, if it is pending.
 *
 * @param c  the callout
 */
void callout_stop(struct callout *c)
{
    if (!c->pending)
        return;

    for (struct callout **p = &c->list->head; *p; p = &(*p)->next) {
        if (*p == c) {
            *p = c->next;
            break;
        }
    }

    c->pending = 0;
    c->list = NULL;
}

/**
 * Sets a callout on a list: `fn(arg)` will be called once `ns` ns have
 * passed, by whoever runs the list. A callout that was pending is
 * cancelled first.
 *
 * @param list  the list it goes on
 * @param c     the callout
 * @param ns    the delay, in ns
 * @param fn    the function to call
 * @param arg   its argument
 */
void callout_set_on(struct callout_list *list, struct callout *c, uint64_t ns,
                    void (*fn)(void *), void *arg)
{
    callout_stop(c);

    c->when = arch_rtime_ns() + ns;
    c->fn   = fn;
    c->arg  = arg;
    c->list = list;
    c->pending = 1;

    c->next = list->head;
    list->head = c;
}

/**
 * Sets a callout on the timer's list: `fn(arg)` will be called from the
 * timer interrupt once `ns` ns have passed.
 *
 * @param c    the callout
 * @param ns   the delay, in ns
 * @param fn   the function to call
 * @param arg  its argument
 */
void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg)
{
    callout_set_on(&timer_callouts, c, ns, fn, arg);
}

/**
 * Calls the callouts of a list that are due, taking each off the list
 * first.
 *
 * @param list  the list
 * @param now   the time, in `arch_rtime_ns()` time
 */
void callout_list_run(struct callout_list *list, uint64_t now)
{
    struct callout **p = &list->head;

    while (*p) {
        struct callout *c = *p;

        if (now < c->when) {
            p = &c->next;
            continue;
        }

        /* take it off the list before calling it: it may set itself again */
        *p = c->next;
        c->pending = 0;
        c->list = NULL;
        c->fn(c->arg);

        /* the function may have changed the list: start over */
        p = &list->head;
    }
}

/**
 * Finds when the first callout of a list is due.
 *
 * @param list  the list
 * @return the time, in `arch_rtime_ns()` time, or 0 if the list is empty
 */
uint64_t callout_list_next(struct callout_list *list)
{
    uint64_t first = 0;

    for (struct callout *c = list->head; c; c = c->next)
        if (!first || c->when < first)
            first = c->when;

    return first;
}

/**
 * Calls the timer's callouts that are due, from the timer interrupt.
 *
 * @param now  the time, in `arch_rtime_ns()` time
 */
void callout_run(uint64_t now)
{
    callout_list_run(&timer_callouts, now);
}
