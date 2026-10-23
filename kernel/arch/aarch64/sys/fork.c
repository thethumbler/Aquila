/** \file
 * aarch64: fork's child thread, which returns to user mode as its parent
 * does: its saved registers copied to its own kernel stack, at its top
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include <aquila/errno.h>

struct aarch64_thread *aarch64_thread_new(void);

int arch_proc_fork(struct thread *thread, struct proc *fork)
{
    struct aarch64_thread *parch = thread->arch;
    struct aarch64_thread *farch = aarch64_thread_new();

    if (!farch)
        return -ENOMEM;

    /* the fork system call's registers, at the top of its stack (where
     * user mode's next exception lands) */
    struct aarch64_regs *regs = (struct aarch64_regs *) (farch->kstack - sizeof(struct aarch64_regs));
    *regs = *parch->regs;
    farch->regs = regs;

    /* it goes on, when first chosen, at aarch64_fork_return (cpu/vectors.S),
     * on that frame */
    extern void aarch64_fork_return(void);
    farch->ctx.lr = (uintptr_t) aarch64_fork_return;
    farch->ctx.sp = (uintptr_t) regs;
    farch->ctx.fp = 0;

    struct thread *fthread = (struct thread *) fork->threads.head->value;
    fthread->arch = farch;

    return 0;
}
