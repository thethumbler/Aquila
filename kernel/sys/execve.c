#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/binfmt.h>
#include <mm/mm.h>

int proc_execve(struct thread *thread, const char *fn, char * const _argp[], char * const _envp[])
{
    struct proc *proc = thread->owner;
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

    argp[argc] = NULL;
    envp[envc] = NULL;

    for (int i = 0; i < argc; ++i)
        argp[i] = strdup(u_argp[i]);

    for (int i = 0; i < envc; ++i)
        envp[i] = strdup(u_envp[i]);

    int err;
    if ((err = binfmt_load(proc, fn, NULL))) {
        /* Free used resources */
        for (int i = 0; i < argc + 1; ++i)
            kfree(argp[i]);
        kfree(argp);

        for (int i = 0; i < envc + 1; ++i)
            kfree(envp[i]);
        kfree(envp);

        return err;
    }

    thread->spawned = 0;
    
    arch_sys_execve(proc, argc + 1, argp, envc + 1, envp);
    memset(proc->sigaction, 0, sizeof(proc->sigaction));

    /* Free used resources */
    for (int i = 0; i < argc + 1; ++i)
        kfree(argp[i]);
    kfree(argp);

    for (int i = 0; i < envc + 1; ++i)
        kfree(envp[i]);
    kfree(envp);
    
    return 0;
}

int thread_execve(struct thread *thread, char * const _argp[], char * const _envp[])
{
    struct proc *proc = thread->owner;

    char **argp = (char **) _argp;
    char **envp = (char **) _envp;

    /* Start at the top of user stack */
    char *stack = (char *) thread->stack_base;

    int argc = 0, envc = 0;
    
    if (envp)
        foreach(env, envp)
            ++envc;

    if (argp)
        foreach(arg, argp)
            ++argc;

    char *u_envp[envc+1];
    char *u_argp[argc+1];

    /* TODO support upward growing stacks */

    /* Push envp strings */
    u_envp[envc] = NULL;
    for (int i = envc - 1; i >= 0; --i) {
        stack -= strlen(envp[i]) + 1;
        strcpy((char *) stack, envp[i]);
        u_envp[i] = (char *) stack;
    }

    /* Push argp strings */
    u_argp[argc] = NULL;
    for (int i = argc - 1; i >= 0; --i) {
        stack -= strlen(argp[i]) + 1;
        strcpy((char *) stack, argp[i]);
        u_argp[i] = (char *) stack;
    }

    /* Push envp array */
    stack -= (envc+1) * sizeof(char *);
    memcpy((void *) stack, u_envp, (envc+1) * sizeof(char *));

    uintptr_t env_ptr = (uintptr_t) stack;

    stack -= (argc+1) * sizeof(char *);
    memcpy((void *) stack, u_argp, (argc+1) * sizeof(char *));

    uintptr_t arg_ptr = (uintptr_t) stack;

    /* main(int argc, char **argv, char **envp) */
    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = env_ptr;

    stack -= sizeof(uintptr_t);
    *(uintptr_t *) stack = arg_ptr;

#if ARCH_BITS==32
    stack -= sizeof(int32_t);
    *(int32_t *) stack = argc;
#else
    stack -= sizeof(int64_t);
    *(int64_t *) stack = argc;
#endif

    thread->stack = (uintptr_t) stack;
    
    return 0;
}
