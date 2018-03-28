#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/binfmt.h>
#include <mm/mm.h>

int proc_execve(thread_t *thread, const char *fn, char * const _argp[], char * const _envp[])
{
    proc_t *proc = thread->owner;
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
