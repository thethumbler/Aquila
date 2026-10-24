/** \file
 * aarch64: system calls. A program makes one with the `svc #0`
 * instruction, as on Linux: the number in x8, the arguments in x0 to x4,
 * and the result comes back in x0. See docs/kernel/syscalls.md.
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

/*
 * Runs a system call: the exception handler calls it for an `svc`
 * instruction from user mode. It returns to user mode when the call is
 * done, delivering the signals that came meanwhile.
 */
void arch_syscall(struct aarch64_regs *r)
{
    /* the number in x8, the arguments in x0 to x4 (as on Linux) */
    syscall_dispatch(r->x[8], r->x[0], r->x[1], r->x[2], r->x[3], r->x[4]);

    /* if the call woke up an urgent thread (the network's, for a packet
     * sent to ourselves), it runs before we return to user mode */
    if (sched_urgent)
        aarch64_preempt();

    /* deliver the pending signals, and restart the call if one
     * interrupted it (not sigreturn, whose registers are the frame's) */
    aarch64_return_to_user(r, curthread->syscall_nr != SYS_sigreturn);
}

/*
 * Sets the result of a thread's system call: x0 when it returns to user
 * mode. That's in its saved registers, or, for a thread that has never
 * run (a child of `fork()`), where it will start from.
 */
void arch_syscall_return(struct thread *thread, uintptr_t val)
{
    struct aarch64_thread *arch = thread->arch;

    if (thread->spawned)
        arch->regs->x[0] = val;
    else
        arch->x0 = val;
}
