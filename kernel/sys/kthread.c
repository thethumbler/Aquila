/*
 * Kernel threads: the threads of process 0, "[kernel]", as the BSDs have
 * it. They run kernel functions on their kernel stacks, on the kernel's
 * address space, and are scheduled as the others are. Process 0 has no
 * parent, its own session and process group, and takes no signals.
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

static int kproc_init(void)
{
    struct proc *proc = kmalloc(sizeof(struct proc), &M_PROC, M_ZERO);

    if (!proc)
        return -ENOMEM;

    proc->pid  = 0;     /* never allocated: init is 1 */
    proc->name = strdup("[kernel]");
    proc->cwd  = strdup("/");
    proc->vm_space.pmap = kvm_space.pmap;
    proc->sig_queue = queue_new();
    proc->running = 1;

    for (int i = 0; i < SIG_MAX; ++i)
        proc->sigaction[i].sa_handler = SIG_DFL;

    if (!proc->name || !proc->cwd || !proc->sig_queue || session_new(proc))
        panic("kthread: can't make process 0");

    enqueue(procs, proc);
    kproc = proc;

    return 0;
}

/* where a kernel thread starts, on its stack (arch_kthread_create) */
static void kthread_start(void)
{
    curthread->kentry(curthread->karg);
    panic("a kernel thread returned");
}

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

    /* switched to as a thread that was switched away from, at its start */
    thread->spawned = 1;
    thread->state = RUNNABLE;
    sched_thread_ready(thread);

    if (ref)
        *ref = thread;

    return 0;
}
