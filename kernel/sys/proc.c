#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/elf.h>

int get_pid()
{
	static int pid = 0;
	return ++pid;
}

proc_t *load_elf(char *fn)
{
	void *arch_specific_data = arch_load_elf();

	printk("Loading file %s\n", fn);
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

	pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URWX);

	proc_t *proc = kmalloc(sizeof(proc_t));
	proc->name = strdup(file->name);
	proc->pid = get_pid();
	proc->heap = proc_heap;
	proc->fds  = kmalloc(FDS_COUNT * sizeof(file_list_t));
	memset(proc->fds, 0, FDS_COUNT * sizeof(file_list_t));

	arch_init_proc(arch_specific_data, proc, hdr.entry);
	arch_load_elf_end(arch_specific_data);

	return proc;
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
			return i;

	return -1;
}