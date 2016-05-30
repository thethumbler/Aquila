#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

void arch_sys_execve(proc_t *proc)
{
	x86_proc_t *arch = proc->arch;

	arch->eip = proc->entry;
	arch->esp = USER_STACK;
	arch->eflags = X86_EFLAGS;
}