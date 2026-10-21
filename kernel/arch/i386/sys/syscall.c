#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <aquila/syscall.h>
#include <aquila/errno.h>

void arch_syscall(struct x86_regs *r)
{
#if ARCH_BITS==32
    if (r->eax >= syscall_cnt || !syscall_table[r->eax]) {
        printk("[%d:%d] %s: undefined syscall %d\n", curproc->pid, curthread->tid, curproc->name, r->eax);
#else
    if (r->rax >= syscall_cnt || !syscall_table[r->rax]) {
        printk("[%d:%d] %s: undefined syscall %ld\n", curproc->pid, curthread->tid, curproc->name, r->rax);
#endif
        arch_syscall_return(curthread,-ENOSYS);
        return;
    }
	
#if ARCH_BITS==32
    curthread->syscall_nr = r->eax;
#else
    curthread->syscall_nr = r->rax;
#endif

#if ARCH_BITS==32
    void (*syscall)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) = syscall_table[r->eax];
    syscall(r->ebx, r->ecx, r->edx, r->esi, r->edi);
#else
    /* the arguments in Linux's registers (r10 for rcx, which the syscall
     * instruction takes) */
    void (*syscall)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) = syscall_table[r->rax];
    syscall(r->rdi, r->rsi, r->rdx, r->r10, r->r8);
#endif

    /* an urgent thread was woken during the call (the network's, by a
     * packet to ourselves): it runs before returning to user mode */
    if (sched_urgent) {
        extern void x86_preempt(void);
        x86_preempt();
    }

    /* the signals, the call restarted if one interrupted it (not
     * sigreturn's: the registers are the frame's) */
    x86_return_to_user(r, curthread->syscall_nr != SYS_sigreturn);
}

void arch_syscall_return(struct thread *thread, uintptr_t val)
{
    struct x86_thread *arch = thread->arch;

#if ARCH_BITS==32
    if (thread->spawned) arch->regs->eax = val;
    else arch->eax = val;
#else
    if (thread->spawned) arch->regs->rax = val;
    else arch->rax = val;
#endif
}
