#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>

void arch_syscall(regs_t *r)
{
	x86_proc_t *arch = cur_proc->arch;
	arch->regs = r;	/* Store a pointer to registers on the stack */

	/* FIXME: Add some out-of-bounds checking code here */
	void (*syscall)() = syscall_table[r->eax];
	syscall(r->ebx, r->ecx, r->edx);
}

void arch_syscall_return(proc_t *proc, uintptr_t val)
{
	x86_proc_t *arch = proc->arch;

	if(proc->spawned)
		arch->regs->eax = val;
	else
		arch->eax = val;
}