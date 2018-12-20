#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <bits/errno.h>

void arch_syscall(struct x86_regs *r)
{
#if ARCH_BITS==32
    if (r->eax >= syscall_cnt) {
        printk("[%d:%d] %s: undefined syscall %d\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, r->eax);
#else
    if (r->rax >= syscall_cnt) {
        printk("[%d:%d] %s: undefined syscall %ld\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, r->rax);
#endif
        arch_syscall_return(cur_thread,-ENOSYS);
        return;
    }
	
#if ARCH_BITS==32
    void (*syscall)() = syscall_table[r->eax];
    syscall(r->ebx, r->ecx, r->edx);
#else
    void (*syscall)() = syscall_table[r->rax];
    syscall(r->rbx, r->rcx, r->rdx);
#endif
}

void arch_syscall_return(thread_t *thread, uintptr_t val)
{
    x86_thread_t *arch = thread->arch;

#if ARCH_BITS==32
    if (thread->spawned) arch->regs->eax = val;
    else arch->eax = val;
#else
    if (thread->spawned) arch->regs->rax = val;
    else arch->rax = val;
#endif
}
