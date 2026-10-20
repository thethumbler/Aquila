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
    char **envp = (char **) _envp;

    /* argc and envc include the terminating NULL entry, the total size
     * of the strings is bounded by the caller (well below the stack size) */
    size_t strings_size = 0;

    for (int i = 0; i < argc - 1; ++i)
        strings_size += strlen(argp[i]) + 1;

    for (int i = 0; i < envc - 1; ++i)
        strings_size += strlen(envp[i]) + 1;

    /* Start at the top of user stack */
    volatile char *stack = (volatile char *) USER_STACK;
    tlb_flush();

    /* Layout (from the top): envp strings, argp strings, envp array,
     * argp array -- the pointer arrays are written directly on the stack */
    char *str = (char *) (stack - strings_size);
    stack = (volatile char *) ((uintptr_t) str & ~(sizeof(uintptr_t) - 1));

    stack -= envc * sizeof(char *);
    char **u_envp = (char **) stack;

    stack -= argc * sizeof(char *);
    char **u_argp = (char **) stack;

    /* Push argp strings */
    for (int i = 0; i < argc - 1; ++i) {
        strcpy(str, argp[i]);
        u_argp[i] = str;
        str += strlen(argp[i]) + 1;
    }

    u_argp[argc - 1] = NULL;

    /* Push envp strings */
    for (int i = 0; i < envc - 1; ++i) {
        strcpy(str, envp[i]);
        u_envp[i] = str;
        str += strlen(envp[i]) + 1;
    }

    u_envp[envc - 1] = NULL;

    uintptr_t env_ptr = (uintptr_t) u_envp;
    uintptr_t arg_ptr = (uintptr_t) u_argp;

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

#if ARCH_BITS==32
    arch->esp = (uintptr_t) stack;
#else
    arch->rsp = (uintptr_t) stack;
#endif
}
