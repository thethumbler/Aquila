/** \file
 * x86: the child's thread, for `fork()`.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <aquila/errno.h>

MALLOC_DECLARE(M_X86_FPU);

/*
 * Sets up the x86 state of a fork's child thread: a copy of the parent's
 * kernel stack and FPU state, and a start at `x86_fork_return`, which
 * returns to user mode through the fork system call's frame, in the copy.
 */
int arch_proc_fork(struct thread *thread, struct proc *fork)
{
    int err = 0;

    struct x86_thread *ptarch = thread->arch;
    struct x86_thread *ftarch = NULL;

    ftarch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);

    if (ftarch == NULL) {
        /* no memory for the child thread's x86 state */
        err = -ENOMEM;
        goto free_resources;
    }

    /* the child's kernel stack */
    uintptr_t fkstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);

    if (!fkstack_base) {
        err = -ENOMEM;
        goto free_resources;
    }

    /* a copy of the parent's kernel stack (in a signal handler, the
     * context it interrupted is at the top, and kstack below it) */
    ftarch->kstack_base = fkstack_base;
    ftarch->kstack = fkstack_base + (ptarch->kstack - ptarch->kstack_base);
    memcpy((void *) fkstack_base, (void *) ptarch->kstack_base, KERN_STACK_SIZE);

    /* the fork system call's registers, at the same place in the copy */
    struct x86_regs *fork_regs = (void *) (fkstack_base + ((uintptr_t) ptarch->regs - ptarch->kstack_base));
    ftarch->regs = fork_regs;

    extern void x86_fork_return();
#if ARCH_BITS==32
    ftarch->eip = (uintptr_t) x86_fork_return;
    ftarch->esp = (uintptr_t) fork_regs;
    ftarch->eflags = ptarch->eflags;
#else
    ftarch->rip = (uintptr_t) x86_fork_return;
    ftarch->rsp = (uintptr_t) fork_regs;
    ftarch->rflags = ptarch->rflags;
#endif

    /* the FPU's state as the parent has it (its registers, MXCSR, the
     * control word), if it used the FPU: the child loads it at its first
     * use */
    ftarch->fpu_enabled = 0;
    ftarch->fpu_context = NULL;

    if (ptarch->fpu_enabled) {
        if (!(ftarch->fpu_context = kmalloc(512, &M_X86_FPU, 0))) {
            err = -ENOMEM;
            goto free_resources;
        }

        ftarch->fpu_enabled = x86_fpu_save_to(ftarch->fpu_context);
    }

    struct thread *fthread = (struct thread *) fork->threads.head->value;
    fthread->arch = ftarch;

    return 0;

free_resources:
    if (ftarch) {
        if (ftarch->kstack_base)
            kfree((void *) ftarch->kstack_base);
        kfree(ftarch);
    }
    return err;
}
