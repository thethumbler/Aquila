#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <aquila/errno.h>

int arch_proc_fork(struct thread *thread, struct proc *fork)
{
    int err = 0;

    struct x86_thread *ptarch = thread->arch;
    struct x86_thread *ftarch = NULL;

    ftarch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, 0);

    if (ftarch == NULL) {
        /* Failed to allocate fork thread arch structure */
        err = -ENOMEM;
        goto free_resources;
    }

    /* Setup kstack */
    uintptr_t fkstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);

    if (!fkstack_base) {
        err = -ENOMEM;
        goto free_resources;
    }

    /* the same stack (in a signal handler, the context it interrupted is
     * on top, kstack below it) */
    ftarch->kstack_base = fkstack_base;
    ftarch->kstack = fkstack_base + (ptarch->kstack - ptarch->kstack_base);
    memcpy((void *) fkstack_base, (void *) ptarch->kstack_base, KERN_STACK_SIZE);

    /* the registers of the fork system call, at the same place */
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

    struct thread *fthread = (struct thread *) fork->threads.head->value;
    fthread->arch = ftarch;

    ftarch->fpu_enabled = 0;
    ftarch->fpu_context = NULL;

    return 0;

free_resources:
    if (ftarch)
        kfree(ftarch);
    return err;
}
