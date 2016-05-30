#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/elf.h>
#include <sys/sched.h>

int get_pid()
{
	static int pid = 0;
	return ++pid;
}

void init_process(proc_t *proc)
{
	proc->pid = get_pid();

	proc->fds  = kmalloc(FDS_COUNT * sizeof(file_list_t));
	memset(proc->fds, 0, FDS_COUNT * sizeof(file_list_t));
}

void kill_process(proc_t *proc)
{
	pmman.unmap((uintptr_t) NULL, (uintptr_t) proc->heap);
	pmman.unmap(USER_STACK_BASE, USER_STACK_BASE);

	//arch_kill_proc(proc);

	kfree(proc->name);
	kfree(proc);
}

int get_fd(proc_t *proc)
{
	for(int i = 0; i < FDS_COUNT; ++i)
		if(!proc->fds[i].inode)
		{
			proc->fds[i].inode = (void*) -1;	
			return i;
		}

	return -1;
}