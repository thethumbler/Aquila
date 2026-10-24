/** \file
 * aarch64: where a new program starts. Its arguments go on its stack, as
 * on x86 (the strings, then the envp and argp arrays), and main()'s argc,
 * argv and envp go in x0 to x2.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>

/*
 * Sets up the user stack and registers of a process's thread for the
 * program that `execve()` loaded.
 */
void arch_sys_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    struct thread *thread = (struct thread *) proc->threads.head->value;
    struct aarch64_thread *arch = thread->arch;

    char **argp = (char **) _argp;
    char **envp = (char **) _envp;

    /* argc and envc count the NULL at the end too */
    size_t strings_size = 0;

    for (int i = 0; i < argc - 1; ++i)
        strings_size += strlen(argp[i]) + 1;

    for (int i = 0; i < envc - 1; ++i)
        strings_size += strlen(envp[i]) + 1;

    /* from the top of the new user stack (the process's address space is
     * the current one, so its pages come in as they're written) */
    char *str = (char *) (USER_STACK - strings_size);
    uintptr_t stack = (uintptr_t) str & ~(sizeof(uintptr_t) - 1);

    stack -= envc * sizeof(char *);
    char **u_envp = (char **) stack;
    stack -= argc * sizeof(char *);
    char **u_argp = (char **) stack;

    for (int i = 0; i < argc - 1; ++i) {
        strcpy(str, argp[i]);
        u_argp[i] = str;
        str += strlen(argp[i]) + 1;
    }

    u_argp[argc - 1] = NULL;

    for (int i = 0; i < envc - 1; ++i) {
        strcpy(str, envp[i]);
        u_envp[i] = str;
        str += strlen(envp[i]) + 1;
    }

    u_envp[envc - 1] = NULL;

    /* the new program's FP/SIMD registers are zeros, at their first use */
    aarch64_fpsimd_drop(thread);

    arch->pc = proc->entry;
    arch->sp = stack & ~(uintptr_t) 15;     /* AAPCS64's 16-byte alignment */
    arch->x0 = argc - 1;
    arch->x1 = (uintptr_t) u_argp;
    arch->x2 = (uintptr_t) u_envp;
}
