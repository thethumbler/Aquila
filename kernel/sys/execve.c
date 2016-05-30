#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>

proc_t *execve_proc(proc_t *proc, const char *fn, char * const argv[] __unused, char * const env[] __unused)
{
	proc_t *p = load_elf_proc(proc, fn);
	
	if(!p)
		return NULL;

	p->spawned = 0;
	
	arch_sys_execve(p);
	
	return p;
}