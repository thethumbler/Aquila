/** \file
 * x86: a thread's CPU state, its kernel stack, and switching to it.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/lock.h>
#include <sys/uaccess.h>
#include <aquila/syscall.h>
#include <ds/queue.h>
#include <mm/pmap.h>

MALLOC_DEFINE(M_KERN_STACK, "kern-stack", "kernel stack");
MALLOC_DEFINE(M_X86_THREAD, "x86-thread", "x86 thread structure");

/* Pushes `value`, of `type`, on the stack at `stack`. */
#define PUSH(stack, type, value)\
do {\
    (stack) -= sizeof(type);\
    *((type *) (stack)) = (type) (value);\
} while (0)

/** Starts the current thread in user mode, for the first time, from its
 * own kernel stack. It lets go of the kernel lock first; once it does,
 * another CPU may run the thread that was running here. */
static void __attribute__((noreturn)) thread_spawn(void)
{
    struct x86_thread *arch = curthread->arch;

    kernel_unlock();

#if ARCH_BITS==32
    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
#else
    x86_jump_user(arch->rax, arch->rip, X86_CS, arch->rflags, arch->rsp, X86_SS);
#endif
}

/*
 * Runs a thread for the first time: switches to its address space and
 * kernel stack, and starts it in user mode. It doesn't return.
 */
void arch_thread_spawn(struct thread *thread)
{
    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);

    /* its first use of the FPU traps (x86_fpu_trap()), as after any
     * switch: the FPU holds another thread's registers */
    x86_fpu_disable();

    x86_goto((uintptr_t) thread_spawn, 0, arch->kstack);
}

/*
 * Switches to a thread that has run before: its address space, its kernel
 * stack, then where it stopped. It doesn't return.
 */
void arch_thread_switch(struct thread *thread)
{
    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);
    x86_fpu_disable();

    /* the signals are delivered on the way back to user mode, not here
     * (x86_return_to_user()): the thread resumes where it was */
#if ARCH_BITS==32
    x86_goto(arch->eip, arch->ebp, arch->esp);
#else
    x86_goto(arch->rip, arch->rbp, arch->rsp);
#endif
}

/*
 * Sets up a new thread's x86 state, for `thread_create()`. The thread
 * will start at `entry` in user mode, on its user stack, below which a
 * frame is pushed: a dummy return address, `uentry` and `arg`.
 */
int arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    /* on failure, thread->arch is left NULL */
    thread->arch = NULL;

    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    if (!arch)
        return -ENOMEM;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);
    if (!kstack) {
        kfree(arch);
        return -ENOMEM;
    }

    uintptr_t frame[3] = {0, uentry, arg};
    stack -= sizeof(frame);

    if (copyout((void *) stack, frame, sizeof(frame))) {
        kfree(kstack);
        kfree(arch);
        return -EFAULT;
    }

    arch->kstack_base = (uintptr_t) kstack;
    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;

    /* a dummy return address */
    PUSH(arch->kstack, void *, 0);

    /* a dummy frame pointer */
    PUSH(arch->kstack, void *, 0);

#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
#endif

#if ARCH_BITS==32
    arch->esp = stack;
#else
    arch->rsp = stack;
#endif
    thread->arch = arch;
    return 0;
}

/*
 * Sets up a kernel thread's x86 state. The thread starts at `entry`, a
 * function without arguments that never returns, on its own kernel stack,
 * once it is switched to.
 */
void arch_kthread_create(struct thread *thread, uintptr_t entry)
{
    /* on failure, thread->arch is left NULL */
    thread->arch = NULL;

    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    if (!arch)
        return;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);
    if (!kstack) {
        kfree(arch);
        return;
    }

    arch->kstack_base = (uintptr_t) kstack;
    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;

    /* as if it had been called: a return address, 0 */
    uintptr_t sp = arch->kstack;
    PUSH(sp, void *, 0);

#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
    arch->esp = sp;
    arch->ebp = 0;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
    arch->rsp = sp;
    arch->rbp = 0;
#endif

    thread->arch = arch;
}

/*
 * Frees a thread's x86 state: its kernel stack and FPU state.
 */
void arch_thread_kill(struct thread *thread)
{
    struct x86_thread *arch = (struct x86_thread *) thread->arch;

    /* its state is gone already */
    if (!arch)
        return;

    if (thread == curthread) {
        /* Its kernel stack is about to be freed: point the TSS at a
         * fixed address meanwhile (no interrupt from user mode comes
         * before the next thread sets its own). */
        uintptr_t esp = VMA(0x100000);
        x86_kernel_stack_set(esp);
    }

    if (arch->kstack_base)
        kfree((void *) arch->kstack_base);

    if (arch->fpu_context)
        kfree(arch->fpu_context);

    /* no CPU's FPU holds its state anymore */
    for (int i = 0; i < cpus_count; ++i)
        if (cpus[i].arch.fpu_thread == thread)
            cpus[i].arch.fpu_thread = NULL;

    kfree(arch);
    thread->arch = NULL;
}

/**
 * Records where the current thread resumes when it is switched back to
 * (`x86_goto()`), with its registers saved on its kernel stack.
 * `x86_sleep` and `x86_preempt` call it, and it saves the thread's FPU
 * state too.
 *
 * @param ip  where it resumes
 * @param sp  its stack pointer there
 * @param bp  its frame pointer there
 */
void x86_thread_saved(uintptr_t ip, uintptr_t sp, uintptr_t bp)
{
    struct x86_thread *arch = curthread->arch;

    x86_fpu_switch_out();

#if ARCH_BITS==32
    arch->eip = ip;
    arch->esp = sp;
    arch->ebp = bp;
#else
    arch->rip = ip;
    arch->rsp = sp;
    arch->rbp = bp;
#endif
}
