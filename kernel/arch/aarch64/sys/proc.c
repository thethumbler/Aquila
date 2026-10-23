/** \file
 * aarch64: init, the first process: its thread, and its program's start
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/panic.h>
#include <mm/pmap.h>
#include <sys/proc.h>
#include <sys/sched.h>

struct aarch64_thread *aarch64_thread_new(void);

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

void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    struct thread *thread = (struct thread *) proc->threads.head->value;

    curthread = thread;
    pmap_switch(proc->vm_space.pmap);

    arch_sys_execve(proc, argc, _argp, envc, _envp);
    curthread = NULL;
}
