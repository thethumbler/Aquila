#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
#include <bits/errno.h>

void arch_syscall(struct x86_regs *r)
{
    if(r->eax > SYSCALL_COUNT)
    {
	printk("[%d:%d] %s: Not Defined Syscall\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
	arch_syscall_return(cur_thread,-ENOSYS);
	return;
    }
	
    void (*syscall)() = syscall_table[r->eax];
    syscall(r->ebx, r->ecx, r->edx);
}

void arch_syscall_return(thread_t *thread, uintptr_t val)
{
    x86_thread_t *arch = thread->arch;

    if (thread->spawned)
        arch->regs->eax = val;
    else
        arch->eax = val;
}
