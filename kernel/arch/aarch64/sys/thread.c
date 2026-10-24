/** \file
 * aarch64: making threads, starting them, switching to them, and ending
 * them.
 *
 * A thread stops in the kernel by saving where it is (aarch64_ctx_save():
 * the registers that a call keeps, and its stack pointer) and calling the
 * scheduler, on its own stack (sys/sched.c). It goes on when that context
 * is loaded again (aarch64_ctx_load()). A thread that never ran starts on
 * its kernel stack, and drops to user mode from there (thread_spawn()).
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <mm/pmap.h>
#include <mm/vm.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/uaccess.h>

MALLOC_DEFINE(M_AARCH64_THREAD, "aarch64-thread", "aarch64 thread structure");
MALLOC_DEFINE(M_KERN_STACK, "kern-stack", "kernel stack");

/**
 * Makes a thread's aarch64 state, with its kernel stack.
 *
 * @return the state, or NULL if there was no memory for it
 */
struct aarch64_thread *aarch64_thread_new(void)
{
    struct aarch64_thread *arch = kmalloc(sizeof(struct aarch64_thread), &M_AARCH64_THREAD, M_ZERO);

    if (!arch)
        return NULL;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);

    if (!kstack) {
        kfree(arch);
        return NULL;
    }

    arch->kstack_base = (uintptr_t) kstack;
    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;
    return arch;
}

/** Runs a thread for the first time: lets go of the kernel lock, and
 * drops to user mode. */
static void __attribute__((noreturn)) thread_spawn(void)
{
    struct aarch64_thread *arch = curthread->arch;

    kernel_unlock();
    aarch64_jump_user(arch->pc, arch->sp, arch->x0, arch->x1, arch->x2, arch->kstack);
}

/*
 * Starts running a thread for the first time, on its kernel stack. It
 * doesn't return.
 */
void arch_thread_spawn(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    pmap_switch(thread->owner->vm_space.pmap);
    aarch64_fpsimd_switch(thread);
    aarch64_goto((uintptr_t) thread_spawn, arch->kstack);
}

/*
 * Switches to a thread that ran before: it goes on from where it was
 * switched away from. It doesn't return.
 */
void arch_thread_switch(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    pmap_switch(thread->owner->vm_space.pmap);
    aarch64_fpsimd_switch(thread);

    /* signals are taken on the way back to user mode
     * (aarch64_return_to_user()), not here: the thread goes on from where
     * it was */
    aarch64_ctx_load(&arch->ctx);
}

/*
 * Sets up a new user thread, for `pthread_create()`.
 */
int arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    struct aarch64_thread *arch = aarch64_thread_new();

    thread->arch = arch;

    if (!arch)
        return -ENOMEM;

    arch->pc = entry;
    arch->sp = stack & ~(uintptr_t) 15;
    arch->x0 = uentry;
    arch->x1 = arg;
    return 0;
}

/*
 * Sets up a kernel thread, which starts at `entry` on its kernel stack
 * once it is switched to. If there's no memory, the thread's state stays
 * NULL.
 */
void arch_kthread_create(struct thread *thread, uintptr_t entry)
{
    struct aarch64_thread *arch = aarch64_thread_new();

    thread->arch = arch;

    if (!arch)
        return;

    /* it's switched to as if it had stopped at its start:
     * aarch64_ctx_load() "returns" into it */
    arch->ctx.lr = entry;
    arch->ctx.sp = arch->kstack;
    arch->ctx.fp = 0;
}

/*
 * Frees a thread's aarch64 state and kernel stack. The thread must not be
 * running on them.
 */
void arch_thread_kill(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    /* killed already */
    if (!arch)
        return;

    aarch64_fpsimd_drop(thread);
    kfree((void *) arch->kstack_base);
    kfree(arch);
    thread->arch = NULL;
}
