#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <ds/queue.h>

int thread_new(struct proc *proc, struct thread **ref)
{
    if (!proc)
        return -EINVAL;

    struct thread *thread;
    thread = kmalloc(sizeof(struct thread));

    if (!thread)
        return -ENOMEM;

    memset(thread, 0, sizeof(struct thread));

    /* TODO Use a better tid allocation method */
    thread->owner = proc;
    proc->threads_nr++;
    thread->tid = proc->threads_nr;

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
    arch_thread_kill(thread);
    thread->state = ZOMBIE;
    return 0;
}

int thread_queue_sleep(struct queue *queue)
{
#ifdef DEBUG_SLEEP_QUEUE
    printk("[%d:%d] %s: Sleeping on queue %p\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, queue);
#endif

    struct qnode *sleep_node = enqueue(queue, cur_thread);

    cur_thread->sleep_queue = queue;
    cur_thread->sleep_node  = sleep_node;
    cur_thread->state = ISLEEP;
    arch_sleep();

    /* Woke up */
    if (cur_thread->state != ISLEEP) {
        /* A signal interrupted the sleep */
#ifdef DEBUG_SLEEP_QUEUE
        printk("[%d:%d] %s: Sleeping was interrupted by a signal\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
#endif
        return -EINTR;
    } else {
        cur_thread->state = RUNNABLE;
        return 0;
    }
}

int thread_queue_wakeup(struct queue *queue)
{
    if (!queue)
        return -EINVAL;

    while (queue->count) {
        struct thread *thread = dequeue(queue);
        thread->sleep_node = NULL;
#ifdef DEBUG_SLEEP_QUEUE
        printk("[%d:%d] %s: Waking up from queue %p\n", thread->owner->pid, thread->tid, thread->owner->name, queue);
#endif
        sched_thread_ready(thread);
    }

    return 0;
}

int thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg, uintptr_t attr __unused, struct thread **new_thread)
{
    struct thread *t = NULL;
    thread_new(thread->owner, &t);

    arch_thread_create(t, stack, entry, uentry, arg);

    if (new_thread)
        *new_thread = t;

    return 0;
}
