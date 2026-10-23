#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

MALLOC_DEFINE(M_THREAD, "thread", "thread structure");

int thread_new(struct proc *proc, struct thread **ref)
{
    struct thread *thread = NULL;

    if (!proc)
        return -EINVAL;

    thread = kmalloc(sizeof(struct thread), &M_THREAD, M_ZERO);
    if (!thread) return -ENOMEM;

    thread->owner = proc;
    thread->tid = ++proc->tid_last;

    enqueue(&proc->threads, thread);

    if (ref)
        *ref = thread;

    return 0;
}

int thread_kill(struct thread *thread)
{
    if (!thread)
        return -EINVAL;

    /* Free resources */
    poll_unregister(thread);
    arch_thread_kill(thread);
    thread->state = ZOMBIE;
    return 0;
}

static void queue_sleep(struct queue *queue, state_t state)
{
    if (!queue)
        panic("sleeping in a blackhole?");

    curthread->sleep_queue = queue;
    curthread->sleep_node  = enqueue(queue, curthread);
    curthread->state = state;
    arch_sleep();
    curthread->state = RUNNABLE;
}

/**
 * Sleep on `queue' until woken up; a signal the process doesn't block
 * ends the sleep (signal_to wakes it): -ERESTARTSYS, for the system call
 * to return (its caller loops on what it waits for, a wakeup may be for
 * something else). Not even asleep if one is pending already.
 */
int thread_queue_sleep(struct queue *queue)
{
    if (signal_interrupts(curthread))
        return -ERESTARTSYS;

    queue_sleep(queue, ISLEEP);

    return signal_interrupts(curthread) ? -ERESTARTSYS : 0;
}

/** sleep on `queue' until woken up, whatever signal comes (a disk
 * transfer, a page being read in: nothing may be left halfway) */
void thread_queue_sleep_unint(struct queue *queue)
{
    queue_sleep(queue, USLEEP);
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
    struct queue *queue;
    struct thread *thread;
    struct poll_reg *next;          /* in its hash chain */
    struct poll_reg *thread_next;   /* the thread's */
};

#define POLL_HASH   64

static struct poll_reg *poll_hash[POLL_HASH];
static int poll_regs;

static struct poll_reg **poll_chain(struct queue *queue)
{
    return &poll_hash[((uintptr_t) queue >> 4) % POLL_HASH];
}

int poll_register(struct queue *queue)
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

/** the pollers of `queue', and those of anything */
static void poll_wake(struct queue *queue)
{
    for (struct poll_reg *r = *poll_chain(queue); r; r = r->next)
        if (r->queue == queue)
            thread_queue_wakeup_nopoll(&r->thread->poll_wait);

    for (struct poll_reg *r = *poll_chain(NULL); r; r = r->next)
        if (!r->queue)
            thread_queue_wakeup_nopoll(&r->thread->poll_wait);
}

int thread_queue_wakeup(struct queue *queue)
{
    if (!queue)
        return -EINVAL;

    if (poll_regs)
        poll_wake(queue);

    return thread_queue_wakeup_nopoll(queue);
}

/** for queues no descriptor is waited on through (a disk request), and
 * the pollers' own: no poller looks at them */
int thread_queue_wakeup_nopoll(struct queue *queue)
{
    if (!queue)
        return -EINVAL;

    while (queue->count) {
        struct thread *thread = dequeue(queue);
        thread->sleep_node = NULL;
#ifdef DEBUG_SLEEP_QUEUE
        printk("[%d:%d] %s: Waking up from queue %p\n", thread->owner->pid, thread->tid, thread->owner->name, queue);
#endif
        sched_thread_wake(thread);
    }

    return 0;
}

/** wake up `thread' only, if it is sleeping on a queue */
void thread_wakeup(struct thread *thread)
{
    if (!thread->sleep_node)
        return;

    queue_node_remove(thread->sleep_queue, thread->sleep_node);
    thread->sleep_node = NULL;
    sched_thread_wake(thread);
}

int thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg, uintptr_t attr __unused, struct thread **new_thread)
{
    int err = 0;
    struct thread *t = NULL;

    if ((err = thread_new(thread->owner, &t)))
        return err;

    if ((err = arch_thread_create(t, stack, entry, uentry, arg))) {
        queue_remove(&thread->owner->threads, t);
        kfree(t);
        return err;
    }

    if (new_thread)
        *new_thread = t;

    return 0;
}
