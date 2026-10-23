/** \file
 * aarch64: threads made, started, switched to, killed
 *
 * A thread stops in the kernel by saving where it is (aarch64_ctx_save():
 * the registers a call keeps, its stack) and calling the scheduler, on
 * its own stack (sys/sched.c); it goes on when that's loaded again
 * (aarch64_ctx_load()). A thread that never ran starts on its kernel
 * stack, from where it drops to user mode (thread_spawn()).
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

/** a thread's architecture part, with its kernel stack; NULL if no memory */
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

/** a thread's first run: the kernel lock let go of, user mode */
static void __attribute__((noreturn)) thread_spawn(void)
{
    struct aarch64_thread *arch = curthread->arch;

    kernel_unlock();
    aarch64_jump_user(arch->pc, arch->sp, arch->x0, arch->x1, arch->x2, arch->kstack);
}

void arch_thread_spawn(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    pmap_switch(thread->owner->vm_space.pmap);
    aarch64_goto((uintptr_t) thread_spawn, arch->kstack);
}

void arch_thread_switch(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    pmap_switch(thread->owner->vm_space.pmap);

    /* signals are taken on the way back to user mode, not here
     * (aarch64_return_to_user): the thread goes on where it was */
    aarch64_ctx_load(&arch->ctx);
}

/** the thread starts at `entry' (the C library's start routine) with the
 * user stack `stack', `uentry' and `arg' its arguments: 0, -ENOMEM */
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

/** a kernel thread, which starts at `entry' (a function without arguments
 * that never returns) on its kernel stack once it is switched to */
void arch_kthread_create(struct thread *thread, uintptr_t entry)
{
    struct aarch64_thread *arch = aarch64_thread_new();

    thread->arch = arch;

    if (!arch)
        return;

    /* switched to as if it had stopped at its start: aarch64_ctx_load()
     * "returns" into it */
    arch->ctx.lr = entry;
    arch->ctx.sp = arch->kstack;
    arch->ctx.fp = 0;
}

void arch_thread_kill(struct thread *thread)
{
    struct aarch64_thread *arch = thread->arch;

    /* already killed */
    if (!arch)
        return;

    kfree((void *) arch->kstack_base);
    kfree(arch);
    thread->arch = NULL;
}
