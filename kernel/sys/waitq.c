/** \file
 * Wait queues (sys/waitq.h): a thread sleeps on one until what it waits
 * for happens and wakes them up; select() and poll() wait for any of
 * several. Sleeping and waking allocate nothing: the sleeping threads
 * are linked through their run_link.
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/waitq.h>

static void sleep_on(struct waitq *waitq, state_t state)
{
    if (!waitq)
        panic("sleeping in a blackhole?");

    curthread->waitq = waitq;
    list_append(&waitq->threads, &curthread->run_link);
    curthread->state = state;
    arch_sleep();
    curthread->state = RUNNABLE;
}

/**
 * Sleep on `waitq' until woken up; a signal the process doesn't block
 * ends the sleep (signal_to wakes it): -ERESTARTSYS, for the system call
 * to return (its caller loops on what it waits for, a wakeup may be for
 * something else). Not even asleep if one is pending already.
 */
int waitq_sleep(struct waitq *waitq)
{
    if (signal_interrupts(curthread))
        return -ERESTARTSYS;

    sleep_on(waitq, ISLEEP);

    return signal_interrupts(curthread) ? -ERESTARTSYS : 0;
}

/** sleep on `waitq' until woken up, whatever signal comes (a disk
 * transfer, a page being read in: nothing may be left halfway) */
void waitq_sleep_unint(struct waitq *waitq)
{
    sleep_on(waitq, USLEEP);
}

/*
 * select() and poll() wait for any of their files: the queue a reader
 * (or writer) of each one sleeps on is registered (poll_register), and
 * the thread sleeps on its own, thread->poll_wait. A wakeup of one of
 * those queues wakes it, to look again; a file that can't say what it
 * sleeps on registers NULL, which any wakeup is.
 */
MALLOC_DEFINE(M_POLL_REG, "poll-reg", "a queue a thread polls");

struct poll_reg {
    struct waitq *queue;
    struct thread *thread;
    struct poll_reg *next;          /* in its hash chain */
    struct poll_reg *thread_next;   /* the thread's */
};

#define POLL_HASH   64

static struct poll_reg *poll_hash[POLL_HASH];
static int poll_regs;

static struct poll_reg **poll_chain(struct waitq *queue)
{
    return &poll_hash[((uintptr_t) queue >> 4) % POLL_HASH];
}

int poll_register(struct waitq *queue)
{
    struct thread *thread = curthread;

    for (struct poll_reg *r = thread->poll_regs; r; r = r->thread_next)
        if (r->queue == queue)
            return 0;

    struct poll_reg *r = kmalloc(sizeof(*r), &M_POLL_REG, 0);

    if (!r)
        return -ENOMEM;

    struct poll_reg **chain = poll_chain(queue);

    r->queue = queue;
    r->thread = thread;
    r->next = *chain;
    *chain = r;
    r->thread_next = thread->poll_regs;
    thread->poll_regs = r;
    poll_regs++;

    return 0;
}

/** `thread' waits no more (it's woken, or it goes) */
void poll_unregister(struct thread *thread)
{
    while (thread->poll_regs) {
        struct poll_reg *r = thread->poll_regs;
        struct poll_reg **p = poll_chain(r->queue);

        while (*p != r)
            p = &(*p)->next;

        *p = r->next;
        thread->poll_regs = r->thread_next;
        poll_regs--;
        kfree(r);
    }
}

/** the pollers of `waitq', and those of anything */
static void poll_wake(struct waitq *waitq)
{
    for (struct poll_reg *r = *poll_chain(waitq); r; r = r->next)
        if (r->queue == waitq)
            waitq_wakeup_nopoll(&r->thread->poll_wait);

    for (struct poll_reg *r = *poll_chain(NULL); r; r = r->next)
        if (!r->queue)
            waitq_wakeup_nopoll(&r->thread->poll_wait);
}

/** every thread sleeping on `waitq' wakes up (ready), and every thread
 * polling it */
void waitq_wakeup(struct waitq *waitq)
{
    if (!waitq)
        return;

    if (poll_regs)
        poll_wake(waitq);

    waitq_wakeup_nopoll(waitq);
}

/** for wait queues no descriptor is waited on through (a disk request),
 * and the pollers' own: no poller looks at them */
void waitq_wakeup_nopoll(struct waitq *waitq)
{
    struct list_node *node;

    if (!waitq)
        return;

    while ((node = list_pop(&waitq->threads))) {
        struct thread *thread = list_entry(node, struct thread, run_link);
        thread->waitq = NULL;
        sched_thread_wake(thread);
    }
}

/** wake up `thread' only, if it is sleeping on a wait queue */
void thread_wakeup(struct thread *thread)
{
    if (!thread->waitq)
        return;

    list_remove(&thread->run_link);
    thread->waitq = NULL;
    sched_thread_wake(thread);
}
