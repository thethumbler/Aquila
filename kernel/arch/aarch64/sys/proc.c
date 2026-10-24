/** \file
 * aarch64: init, the first process: its thread, and where its program
 * starts.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/pmap.h>
#include <sys/proc.h>
#include <sys/sched.h>

struct aarch64_thread *aarch64_thread_new(void);

/*
 * Sets up the architecture's part of init's thread, to start at its
 * program's entry point. Panics if there's no memory.
 */
void arch_proc_init(struct proc *proc)
{
    struct aarch64_thread *arch = aarch64_thread_new();

    if (!arch)
        panic("no memory for init's thread");

    arch->pc = proc->entry;
    arch->sp = USER_STACK;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->arch = arch;
}

/*
 * Sets up init's user stack and registers for its program, from the
 * kernel's boot (no process runs yet).
 */
void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    struct thread *thread = (struct thread *) proc->threads.head->value;

    curthread = thread;
    pmap_switch(proc->vm_space.pmap);

    arch_sys_execve(proc, argc, _argp, envc, _envp);
    curthread = NULL;
}
