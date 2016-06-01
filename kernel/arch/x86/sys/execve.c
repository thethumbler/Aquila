#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>

void arch_sys_execve(proc_t *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
	x86_proc_t *arch = proc->arch;

	arch->eip = proc->entry;
	arch->eflags = X86_EFLAGS;

	char **argp = (char **) _argp;
	char **u_argp = kmalloc(argc * sizeof(char *));

	char **envp = (char **) _envp;
	char **u_envp = kmalloc(envc * sizeof(char *));

	/* Push envp and argp to user stack */
	uintptr_t stack = USER_STACK;

	stack -= 4;
	*(char **) stack = NULL;
	
	int tmp_envc = envc;

	for(int i = envc - 1; i >= 0; --i)
	{
		if(envp[i])
		{
			stack -= strlen(envp[i]) + 1;
			strcpy((char *) stack, envp[i]);
			u_envp[--tmp_envc] = (char *) stack;
		} else
		{
			u_envp[--tmp_envc] = (char *) NULL;
		}
	}

	int tmp_argc = argc;

	for(int i = argc - 1; i >= 0; --i)
	{
		if(argp[i])
		{
			stack -= strlen(argp[i]) + 1;
			strcpy((char *) stack, argp[i]);
			u_argp[--tmp_argc] = (char *) stack;
		} else
		{
			u_argp[--tmp_argc] = (char *) NULL;
		}
	}

	stack -= envc * sizeof(char *);
	memcpy((void *) stack, u_envp, envc * sizeof(char *));

	stack -= argc * sizeof(char *);
	memcpy((void *) stack, u_argp, argc * sizeof(char *));

	stack -= sizeof(int);
	*(int *) stack = argc;

	kfree(u_envp);
	kfree(u_argp);

	arch->esp = stack;
}