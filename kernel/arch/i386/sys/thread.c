#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <aquila/syscall.h>
#include <ds/queue.h>
#include <mm/pmap.h>

MALLOC_DEFINE(M_KERN_STACK, "kern-stack", "kernel stack");
MALLOC_DEFINE(M_X86_THREAD, "x86-thread", "x86 thread structure");

#define PUSH(stack, type, value)\
do {\
    (stack) -= sizeof(type);\
    *((type *) (stack)) = (type) (value);\
} while (0)

void arch_thread_spawn(struct thread *thread)
{
    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);

#if ARCH_BITS==32
    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
#else
    x86_jump_user(arch->rax, arch->rip, X86_CS, arch->rflags, arch->rsp, X86_SS);
#endif
}

void arch_thread_switch(struct thread *thread)
{
    //printk("[%d:%d] %s: arch_thread_switch(thread=%p)\n", thread->owner->pid, thread->tid, thread->owner->name, thread);

    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);
    x86_fpu_disable();

    /* (an exiting process has no signal queue left) */
    int sig = thread->sig_defer || thread->in_fault || !thread->owner->running ?
        0 : signal_next(thread->owner);

    /* the thread can't stop here, it takes the stop when it can sleep:
     * when it wakes up, or before returning to user mode */
    if (sig && signal_stops(thread->owner, sig)) {
        thread->owner->stop_pending = sig;
        sig = 0;
    }

    if (sig) {
        /* interrupting sigsuspend(), the handler runs with the old mask */
        if (thread->sigsuspend) {
            thread->owner->sigmask = thread->sigsuspend_mask;
            thread->sigsuspend = 0;
        }

        /* a system call blocked in the kernel fails with EINTR, otherwise
         * the handler would return to it with the syscall number in eax;
         * or, SA_RESTART, it is made again once the handler returns (not
         * the waits with a time, POSIX's exceptions) */
        if (thread->in_syscall) {
            int nr = thread->syscall_nr;
            int restart = (thread->owner->sigaction[sig].sa_flags & SA_RESTART) &&
                nr != SYS_nanosleep && nr != SYS_select && nr != SYS_poll &&
                nr != SYS_sigsuspend;

            /* it doesn't go back to the wait it was in, nor to its end */
            sched_timer_cancel(thread);

            if (restart) {
                /* back to the int $0x80, with the number in eax */
#if ARCH_BITS==32
                arch->regs->eip -= 2;
                arch->regs->eax = nr;
#else
                arch->regs->rip -= 2;
                arch->regs->rax = nr;
#endif
                thread->in_syscall = 0;
            } else {
                sched_sleep_interrupted(thread);
                arch_syscall_return(thread, -EINTR);
            }
        }

        arch_handle_signal(sig);
        /* if we get back here, the signal was ignored */
    }

#if ARCH_BITS==32
    x86_goto(arch->eip, arch->ebp, arch->esp);
#else
    x86_goto(arch->rip, arch->rbp, arch->rsp);
#endif
}

void arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    /* thread->arch is left NULL on failure */
    thread->arch = NULL;

    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    if (!arch)
        return;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);
    if (!kstack) {
        kfree(arch);
        return;
    }

    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;

    /* dummy return address */
    PUSH(arch->kstack, void *, 0);

    /* dummy base pointer */
    PUSH(arch->kstack, void *, 0);

#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
#endif

    /* Push thread argument */
    PUSH(stack, void *, arg);

    /* Push user entry point */
    PUSH(stack, void *, uentry);

    /* Dummy return address */
    PUSH(stack, void *, 0);

#if ARCH_BITS==32
    arch->esp = stack;
#else
    arch->rsp = stack;
#endif
    thread->arch = arch;
}

void arch_thread_kill(struct thread *thread)
{
    struct x86_thread *arch = (struct x86_thread *) thread->arch;

    /* already killed */
    if (!arch)
        return;

    if (thread == curthread) {
        /* We don't wanna die here */
        uintptr_t esp = VMA(0x100000); /* XXX */
        x86_kernel_stack_set(esp);
    }

    if (arch->kstack)
        kfree((void *) (arch->kstack - KERN_STACK_SIZE));

    if (arch->fpu_context)
        kfree(arch->fpu_context);

    extern struct thread *last_fpu_thread;
    if (last_fpu_thread == thread)
        last_fpu_thread = NULL;

    kfree(arch);
    thread->arch = NULL;
}

/*
 * Where the current thread resumes when it is switched back to (x86_goto):
 * x86_sleep and x86_preempt record it, its registers on its kernel stack
 */
void x86_thread_saved(uintptr_t ip, uintptr_t sp, uintptr_t bp)
{
    struct x86_thread *arch = curthread->arch;

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
