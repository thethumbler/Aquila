/** \file
 * Kernel threads.
 *
 * Kernel threads are the threads of process 0, "[kernel]", as on the
 * BSDs. They run kernel functions on their kernel stacks, in the kernel's
 * address space, and are scheduled like any other thread. Process 0 has
 * no parent, has its own session and process group, and takes no
 * signals.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <core/string.h>
#include <mm/vm.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <ds/queue.h>

MALLOC_DECLARE(M_PROC);

struct proc *kproc;

/** Makes process 0, the kernel threads' process, the first time a kernel
 * thread is made. Returns 0, or `-ENOMEM`. */
static int kproc_init(void)
{
    struct proc *proc = kmalloc(sizeof(struct proc), &M_PROC, M_ZERO);

    if (!proc)
        return -ENOMEM;

    proc->pid  = 0;     /* never allocated: init is 1 */
    proc->name = strdup("[kernel]");

    if (!proc->name)
        panic("no memory for process 0");
    proc->vm_space.pmap = kvm_space.pmap;
    proc->running = 1;

    for (int i = 0; i < SIG_MAX; ++i)
        proc->sigaction[i].sa_handler = SIG_DFL;

    /* the kernel can't boot without process 0 */
    if (!proc->name || session_new(proc) || !enqueue(procs, proc))
        panic("kthread: can't make process 0");

    kproc = proc;

    return 0;
}

/** Runs a kernel thread's function: every kernel thread starts here, on
 * its own stack (`arch_kthread_create()`). The function must not return. */
static void kthread_start(void)
{
    curthread->kentry(curthread->karg);
    panic("a kernel thread returned");
}

/**
 * Makes a kernel thread, and makes it ready to run.
 *
 * @param name  the thread's name, as `ps` shows it ("[net]")
 * @param fn    the function it runs, which must never return
 * @param arg   the argument `fn` is called with
 * @param ref   if not NULL, set to the new thread
 * @return 0, or `-ENOMEM` if there was no memory for the thread
 */
int kthread_create(const char *name, void (*fn)(void *), void *arg, struct thread **ref)
{
    int err;
    struct thread *thread = NULL;

    if (!kproc && (err = kproc_init()))
        return err;

    if ((err = thread_new(kproc, &thread)))
        return err;

    thread->name   = name;
    thread->kentry = fn;
    thread->karg   = arg;

    arch_kthread_create(thread, (uintptr_t) kthread_start);

    if (!thread->arch) {
        queue_remove(&kproc->threads, thread);
        kfree(thread);
        return -ENOMEM;
    }

    /* the scheduler switches to it as to any thread that was switched
     * away from: its saved context is its start */
    thread->spawned = 1;
    thread->state = RUNNABLE;
    sched_thread_ready(thread);

    if (ref)
        *ref = thread;

    return 0;
}
