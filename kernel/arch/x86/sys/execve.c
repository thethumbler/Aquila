#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>

void arch_sys_execve(proc_t *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    thread_t *thread = (thread_t *) proc->threads.head->value;
    x86_thread_t *arch = thread->arch;

    arch->eip = proc->entry;
    arch->eflags = X86_EFLAGS;

    char **argp = (char **) _argp;
    char **u_argp = kmalloc(argc * sizeof(char *));

    char **envp = (char **) _envp;
    char **u_envp = kmalloc(envc * sizeof(char *));

    /* Start at the top of user stack */
    uintptr_t stack = USER_STACK;

    /* Push envp strings */
    int tmp_envc = envc;
    u_envp[--tmp_envc] = NULL;

    for (int i = envc - 2; i >= 0; --i) {
        stack -= strlen(envp[i]) + 1;
        strcpy((char *) stack, envp[i]);
        u_envp[--tmp_envc] = (char *) stack;
    }

    /* Push argp strings */
    int tmp_argc = argc;
    u_argp[--tmp_argc] = NULL;

    for (int i = argc - 2; i >= 0; --i) {
        stack -= strlen(argp[i]) + 1;
        strcpy((char *) stack, argp[i]);
        u_argp[--tmp_argc] = (char *) stack;
    }

    stack -= envc * sizeof(char *);
    memcpy((void *) stack, u_envp, envc * sizeof(char *));

    uintptr_t env_ptr = stack;

    stack -= argc * sizeof(char *);
    memcpy((void *) stack, u_argp, argc * sizeof(char *));

    uintptr_t arg_ptr = stack;

    /* main(int argc, char **argv, char **envp) */
    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = env_ptr;

    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = arg_ptr;

    stack -= sizeof(int);
    *(int *) stack = argc - 1;

    kfree(u_envp);
    kfree(u_argp);

    arch->esp = stack;
}
