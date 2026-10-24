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

    if (!enqueue(&proc->threads, thread)) {
        kfree(thread);
        return -ENOMEM;
    }

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
