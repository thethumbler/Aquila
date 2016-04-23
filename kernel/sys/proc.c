#include <core/system.h>
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
	printk("Loading file %s\n", fn);
	inode_t *file = vfs.find(NULL, fn);
	if(!file) return NULL;

	elf32_hdr_t hdr;
	vfs.read(file, 0, sizeof(hdr), &hdr);

	size_t offset = hdr.shoff;
	
	for(int i = 0; i < hdr.shnum; ++i)
	{
		elf32_section_hdr_t shdr;
		vfs.read(file, offset, sizeof(shdr), &shdr);
		
		if(shdr.flags & SHF_ALLOC && shdr.type == SHT_PROGBITS)
		{
			printk("ALLOC %x [%d]\n", shdr.addr, shdr.size);
			/* FIXME add some out-of-bounds handling code here */
			pmman.map(shdr.addr, shdr.size, URWX);	/* FIXME URWX, are you serious? */
			//vfs.read(file, shdr.off, shdr.size, (void*) shdr.addr);
		}

		offset += hdr.shentsize;
	}

	proc_t *proc = kmalloc(sizeof(proc_t));
	proc->pid = get_pid();

	return proc;
}