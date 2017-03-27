#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/elf.h>
#include <mm/mm.h>

proc_t *execve_proc(proc_t *proc, const char *fn, char * const _argp[], char * const _envp[])
{
	char **u_argp = (char **) _argp;
	char **u_envp = (char **) _envp;

	int argc = 0, envc = 0;
	
	if (u_argp)
		foreach(arg, u_argp)
			++argc;
	if (u_envp)
		foreach(env, u_envp)
			++envc;

	char **argp = kmalloc((argc + 1) * sizeof(char *));
	char **envp = kmalloc((envc + 1) * sizeof(char *));

	argp[0] = strdup(fn);
	envp[envc] = NULL;

	for (int i = 0; i < argc; ++i) {
		if (u_argp[i])
			argp[i + 1] = strdup(u_argp[i]);
		else
			argp[i + 1] = NULL;
    }

	for (int i = 0; i < envc; ++i) {
		if (u_envp[i])
			envp[i] = strdup(u_envp[i]);
		else
			envp[i] = NULL;
    }

	proc_t *p = load_elf_proc(proc, fn);
	
	if (!p) {
		/* Free used resources */
		for (int i = 0; i < argc + 1; ++i)
			kfree(argp[i]);
		kfree(argp);

		for (int i = 0; i < envc + 1; ++i)
			kfree(envp[i]);
		kfree(envp);

		return NULL;
	}

	p->spawned = 0;
	
	arch_sys_execve(p, argc + 1, argp, envc + 1, envp);

	/* Free used resources */
	for (int i = 0; i < argc + 1; ++i)
		kfree(argp[i]);
	kfree(argp);

	for (int i = 0; i < envc + 1; ++i)
		kfree(envp[i]);
	kfree(envp);
	
	return p;
}
