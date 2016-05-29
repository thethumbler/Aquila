#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <core/arch.h>
#include <sys/proc.h>

proc_t *fork_proc(proc_t *proc)
{
	proc_t *fork = kmalloc(sizeof(proc_t));
	memcpy(fork, proc, sizeof(proc_t));

	fork->name = strdup(proc->name);
	fork->pid = get_pid();
	fork->parent = proc;
	fork->spawned = 0;
	
	fork->fds = kmalloc(FDS_COUNT * sizeof(file_list_t));
	memcpy(fork->fds, proc->fds, FDS_COUNT * sizeof(file_list_t));

	arch_sys_fork(fork);

	arch_syscall_return(fork, 0);
	arch_syscall_return(proc, fork->pid);

	return fork;
}