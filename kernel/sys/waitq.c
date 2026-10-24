/** \file
 * Wait queues (see `sys/waitq.h`). A thread that has to wait for
 * something sleeps on the wait queue that belongs to it, and whatever
 * makes it happen wakes the queue's threads up. `select()` and `poll()`
 * wait for any of several queues at once.
 *
 * Sleeping and waking up allocate nothing: a sleeping thread is linked
 * into its wait queue through its own `run_link`, the same link that
 * puts it on the ready list when it can run.
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/waitq.h>

/**
 * Puts the current thread to sleep on `waitq` in `state` (`ISLEEP` or
 * `USLEEP`), and returns once it has been woken up and has run again.
 */
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
 * Puts the current thread to sleep on a wait queue until it is woken up.
 *
 * A signal that the process doesn't block also ends the sleep
 * (`signal_to()` wakes the thread). The system call should then return,
 * so that the signal can be delivered. If a signal is already pending,
 * the thread doesn't sleep at all.
 *
 * A wakeup only means that something changed, so callers sleep in a loop
 * and look again at what they wait for each time.
 *
 * @param waitq  the wait queue to sleep on
 * @return 0 once woken up, or `-ERESTARTSYS` if a signal ended the sleep
 */
int waitq_sleep(struct waitq *waitq)
{
    if (signal_interrupts(curthread))
        return -ERESTARTSYS;

    sleep_on(waitq, ISLEEP);

    return signal_interrupts(curthread) ? -ERESTARTSYS : 0;
}

/**
 * Puts the current thread to sleep on a wait queue until it is woken up,
 * whatever signals come meanwhile. This is for waits that can't be
 * abandoned halfway, such as a disk transfer or a page being read in.
 *
 * @param waitq  the wait queue to sleep on
 */
void waitq_sleep_unint(struct waitq *waitq)
{
    sleep_on(waitq, USLEEP);
}

/*
 * select() and poll() wait for any of several files. For each file, the
 * thread registers the wait queue that the file's readers (or writers)
 * sleep on (poll_register()), then sleeps on its own queue,
 * `thread->poll_wait`. A wakeup of any registered queue also wakes that
 * thread, which then looks at its files again. A file that can't say
 * which queue it uses registers NULL, which every wakeup matches.
 *
 * Registrations are kept in a small hash table, keyed by the queue, so
 * that a wakeup finds its pollers without looking at every thread.
 */
MALLOC_DEFINE(M_POLL_REG, "poll-reg", "a queue a thread polls");

/** One wait queue that one polling thread waits for. */
struct poll_reg {
    struct waitq *queue;
    struct thread *thread;
    struct poll_reg *next;          /* the next one in its hash chain */
    struct poll_reg *thread_next;   /* the thread's next registration */
};

#define POLL_HASH   64

static struct poll_reg *poll_hash[POLL_HASH];
static int poll_regs;

/** Returns the hash chain that registrations of `queue` are on. */
static struct poll_reg **poll_chain(struct waitq *queue)
{
    return &poll_hash[((uintptr_t) queue >> 4) % POLL_HASH];
}

/**
 * Registers a wait queue that the current thread, in `select()` or
 * `poll()`, waits for: a wakeup of that queue also wakes the thread.
 * Registering the same queue twice does nothing.
 *
 * @param queue  the wait queue, or NULL to be woken by any wakeup
 * @return 0, or `-ENOMEM` if there was no memory for the registration
 */
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

/**
 * Removes every registration of a thread, when it stops waiting (it was
 * woken up, or it is going away).
 *
 * @param thread  the thread whose registrations go
 */
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

/** Wakes up the threads polling `waitq`, and those polling any queue. */
static void poll_wake(struct waitq *waitq)
{
    for (struct poll_reg *r = *poll_chain(waitq); r; r = r->next)
        if (r->queue == waitq)
            waitq_wakeup_nopoll(&r->thread->poll_wait);

    for (struct poll_reg *r = *poll_chain(NULL); r; r = r->next)
        if (!r->queue)
            waitq_wakeup_nopoll(&r->thread->poll_wait);
}

/**
 * Wakes up every thread sleeping on a wait queue, and every thread
 * polling it. The threads become ready to run.
 *
 * @param waitq  the wait queue (NULL does nothing)
 */
void waitq_wakeup(struct waitq *waitq)
{
    if (!waitq)
        return;

    if (poll_regs)
        poll_wake(waitq);

    waitq_wakeup_nopoll(waitq);
}

/**
 * Wakes up every thread sleeping on a wait queue, but not the threads
 * polling it. This is for queues that no file descriptor waits through,
 * such as a disk request's, and for the pollers' own queues.
 *
 * @param waitq  the wait queue (NULL does nothing)
 */
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

/**
 * Wakes up one thread, if it is sleeping on a wait queue. The other
 * threads on that queue keep sleeping.
 *
 * @param thread  the thread to wake up
 */
void thread_wakeup(struct thread *thread)
{
    if (!thread->waitq)
        return;

    list_remove(&thread->run_link);
    thread->waitq = NULL;
    sched_thread_wake(thread);
}
