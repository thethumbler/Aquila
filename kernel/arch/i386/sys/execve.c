/** \file
 * x86: a new program's first registers and its stack, for `execve()`.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>

/*
 * Sets up a process's thread to start a new program: its first
 * instruction, and its user stack with the arguments and the environment
 * (argc, argv and envp, then their strings), as crt0 expects them.
 */
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

    /* argc and envc count the terminating NULL entries too; the caller
     * bounds the total size of the strings, well below the stack's size */
    size_t strings_size = 0;

    for (int i = 0; i < argc - 1; ++i)
        strings_size += strlen(argp[i]) + 1;

    for (int i = 0; i < envc - 1; ++i)
        strings_size += strlen(envp[i]) + 1;

    /* Start at the top of the user stack that the new program was just
     * given. It is in the current address space, and its pages fault in
     * as they are written, so this doesn't need copyout(). */
    volatile char *stack = (volatile char *) USER_STACK;
    tlb_flush();

    /* The layout, from the top: the strings (argv's, then envp's above
     * them in memory), envp's array, then argv's array, written directly
     * on the stack. */
    char *str = (char *) (stack - strings_size);
    stack = (volatile char *) ((uintptr_t) str & ~(sizeof(uintptr_t) - 1));

    stack -= envc * sizeof(char *);
    char **u_envp = (char **) stack;

    stack -= argc * sizeof(char *);
    char **u_argp = (char **) stack;

    /* argv's strings */
    for (int i = 0; i < argc - 1; ++i) {
        strcpy(str, argp[i]);
        u_argp[i] = str;
        str += strlen(argp[i]) + 1;
    }

    u_argp[argc - 1] = NULL;

    /* envp's strings */
    for (int i = 0; i < envc - 1; ++i) {
        strcpy(str, envp[i]);
        u_envp[i] = str;
        str += strlen(envp[i]) + 1;
    }

    u_envp[envc - 1] = NULL;

    uintptr_t env_ptr = (uintptr_t) u_envp;
    uintptr_t arg_ptr = (uintptr_t) u_argp;

    /* the ABI wants the stack 16-byte aligned at the program's entry,
     * once argc, argv and envp below are pushed */
#if ARCH_BITS==32
    size_t frame = 2 * sizeof(uintptr_t) + sizeof(int32_t);
#else
    size_t frame = 2 * sizeof(uintptr_t) + sizeof(int64_t);
#endif
    stack = (volatile char *) ((((uintptr_t) stack - frame) & ~(uintptr_t) 15) + frame);

    /* what crt0 passes on to main(int argc, char **argv, char **envp) */
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
