/** \file
 * aarch64: system calls, `svc #0', as Linux's: the number in x8, the
 * arguments in x0 to x4, the result back in x0 (syscalls.md)
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <aquila/syscall.h>
#include <aquila/errno.h>

void aarch64_preempt(void);

void arch_syscall(struct aarch64_regs *r)
{
    /* the number in x8, the arguments in x0 to x4 (Linux's) */
    syscall_dispatch(r->x[8], r->x[0], r->x[1], r->x[2], r->x[3], r->x[4]);

    /* an urgent thread was woken during the call (the network's, by a
     * packet to ourselves): it runs before returning to user mode */
    if (sched_urgent)
        aarch64_preempt();

    /* the signals, the call restarted if one interrupted it (not
     * sigreturn's: the registers are the frame's) */
    aarch64_return_to_user(r, curthread->syscall_nr != SYS_sigreturn);
}

/** a call's result: in x0 when the thread returns to user mode (its
 * saved registers; a thread that never ran, where it starts) */
void arch_syscall_return(struct thread *thread, uintptr_t val)
{
    struct aarch64_thread *arch = thread->arch;

    if (thread->spawned)
        arch->regs->x[0] = val;
    else
        arch->x0 = val;
}
