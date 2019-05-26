#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <bits/errno.h>

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
    ftarch->kstack = fkstack_base + KERN_STACK_SIZE;

    /* Copy registers */
    struct x86_regs *fork_regs = (void *) (ftarch->kstack - (ptarch->kstack - (uintptr_t) ptarch->regs));
    ftarch->regs = fork_regs;

    /* Copy kstack */
    memcpy((void *) fkstack_base, (void *) (ptarch->kstack - KERN_STACK_SIZE), KERN_STACK_SIZE);

    extern void x86_fork_return();
#if ARCH_BITS==32
    ftarch->eip = (uintptr_t) x86_fork_return;
    ftarch->esp = (uintptr_t) fork_regs;
#else
    ftarch->rip = (uintptr_t) x86_fork_return;
    ftarch->rsp = (uintptr_t) fork_regs;
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
