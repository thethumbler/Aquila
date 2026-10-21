/*
 * Callouts (sys/callout.h): functions called at a time to come. Each list
 * is run from one context: the timer's from its interrupt
 * (sched_timer_tick), which checks it on each tick; others by the thread
 * that has it, which sleeps until the first is due (callout_list_next).
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/callout.h>

static struct callout_list timer_callouts;

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

void callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg)
{
    callout_set_on(&timer_callouts, c, ns, fn, arg);
}

void callout_list_run(struct callout_list *list, uint64_t now)
{
    struct callout **p = &list->head;

    while (*p) {
        struct callout *c = *p;

        if (now < c->when) {
            p = &c->next;
            continue;
        }

        /* off the list before it is called, it may set itself again */
        *p = c->next;
        c->pending = 0;
        c->list = NULL;
        c->fn(c->arg);

        /* the list may have changed: from the start */
        p = &list->head;
    }
}

uint64_t callout_list_next(struct callout_list *list)
{
    uint64_t first = 0;

    for (struct callout *c = list->head; c; c = c->next)
        if (!first || c->when < first)
            first = c->when;

    return first;
}

void callout_run(uint64_t now)
{
    callout_list_run(&timer_callouts, now);
}
