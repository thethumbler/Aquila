#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>

void arch_sys_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    struct thread *thread = (struct thread *) proc->threads.head->value;
    struct x86_thread *arch = thread->arch;

#if ARCH_BITS==32
    arch->eip = proc->entry;
    arch->eflags = X86_EFLAGS;
#else
    arch->rip = proc->entry;
    arch->rflags = X86_EFLAGS;
#endif

    char **argp = (char **) _argp;
    char **u_argp = kmalloc(argc * sizeof(char *), &M_BUFFER, 0);

    char **envp = (char **) _envp;
    char **u_envp = kmalloc(envc * sizeof(char *), &M_BUFFER, 0);

    /* Start at the top of user stack */
    volatile char *stack = (volatile char *) USER_STACK;
    tlb_flush();

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

    uintptr_t env_ptr = (uintptr_t) stack;

    stack -= argc * sizeof(char *);
    memcpy((void *) stack, u_argp, argc * sizeof(char *));

    uintptr_t arg_ptr = (uintptr_t) stack;

    /* main(int argc, char **argv, char **envp) */
    stack -= sizeof(uintptr_t);
    *(volatile uintptr_t *) stack = env_ptr;

    stack -= sizeof(uintptr_t);
    *(volatile uintptr_t *) stack = arg_ptr;

#if ARCH_BITS==32
    stack -= sizeof(int32_t);
    *(volatile int32_t *) stack = argc - 1;
#else
    stack -= sizeof(int64_t);
    *(volatile int64_t *) stack = argc - 1;
#endif

    kfree(u_envp);
    kfree(u_argp);

#if ARCH_BITS==32
    arch->esp = (uintptr_t) stack;
#else
    arch->rsp = (uintptr_t) stack;
#endif
}
