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

proc_t *load_elf(const char *fn)
{
	void *arch_specific_data = arch_load_elf();

	inode_t *file = vfs.find(vfs_root, fn);
	if(!file) return NULL;

	elf32_hdr_t hdr;
	vfs.read(file, 0, sizeof(hdr), &hdr);

	void *proc_heap = NULL;
	size_t offset = hdr.shoff;
	
	for(int i = 0; i < hdr.shnum; ++i)
	{
		elf32_section_hdr_t shdr;
		vfs.read(file, offset, sizeof(shdr), &shdr);
		
		if(shdr.flags & SHF_ALLOC && shdr.type == SHT_PROGBITS)
		{
			/* FIXME add some out-of-bounds handling code here */
			pmman.map(shdr.addr, shdr.size, URWX);	/* FIXME URWX, are you serious? */
			vfs.read(file, shdr.off, shdr.size, (void*) shdr.addr);

			if(shdr.addr + shdr.size > (uintptr_t) proc_heap)
				proc_heap = (void *) (shdr.addr + shdr.size);
		}

		offset += hdr.shentsize;
	}

	pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URW);
	pmman.map(USER_ENV, USER_ENV_SIZE, URW);

	proc_t *proc = kmalloc(sizeof(proc_t));
	proc->name = strdup(file->name);
	proc->heap = proc_heap;

	arch_init_proc(arch_specific_data, proc, hdr.entry);
	arch_load_elf_end(arch_specific_data);

	return proc;
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

	arch_kill_process(proc);

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

proc_t *fork_process(proc_t *proc)
{
	proc_t *fork = kmalloc(sizeof(proc_t));
	memcpy(fork, proc, sizeof(proc_t));

	fork->name = strdup(proc->name);
	fork->pid = get_pid();
	
	fork->fds = kmalloc(FDS_COUNT * sizeof(file_list_t));
	memcpy(fork->fds, proc->fds, FDS_COUNT * sizeof(file_list_t));

	arch_sys_fork(fork);

	arch_user_return(fork, 0);
	arch_user_return(proc, fork->pid);

	return fork;
}

proc_t *execve_elf(proc_t *proc, const char *fn, char * const argv[] __unused, char * const env[] __unused)
{
	inode_t *file = vfs.find(vfs_root, fn);
	if(!file) return NULL;

	elf32_hdr_t hdr;
	vfs.read(file, 0, sizeof(hdr), &hdr);

	void *proc_heap = NULL;
	size_t offset = hdr.shoff;
	
	for(int i = 0; i < hdr.shnum; ++i)
	{
		elf32_section_hdr_t shdr;
		vfs.read(file, offset, sizeof(shdr), &shdr);
		
		if(shdr.flags & SHF_ALLOC && shdr.type == SHT_PROGBITS)
		{
			/* FIXME add some out-of-bounds handling code here */
			pmman.map(shdr.addr, shdr.size, URWX);	/* FIXME URWX, are you serious? */
			vfs.read(file, shdr.off, shdr.size, (void*) shdr.addr);

			if(shdr.addr + shdr.size > (uintptr_t) proc_heap)
				proc_heap = (void *) (shdr.addr + shdr.size);
		}

		offset += hdr.shentsize;
	}

	kfree(proc->name);
	proc->name = strdup(file->name);

	if(proc->heap > proc_heap)
		pmman.unmap((uintptr_t) proc_heap, proc->heap - proc_heap);
	proc->heap = proc_heap;

	arch_execve_proc(proc, hdr.entry);

	return proc;
}