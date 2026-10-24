/** \file
 * x86: the first process's thread.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>
#include <mm/pmap.h>

/*
 * Sets up the x86 state of init's thread, at boot: a kernel stack, and a
 * start at the program's entry point in user mode. Panics if there's no
 * memory: the kernel can't go on without init.
 */
void arch_proc_init(struct proc *proc)
{
    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    uintptr_t kstack_base = arch ? (uintptr_t) kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0) : 0;

    if (!kstack_base)
        panic("no memory for init's thread");

    arch->kstack_base = kstack_base;
    arch->kstack = kstack_base + KERN_STACK_SIZE;   /* its top */
#if ARCH_BITS==32
    arch->eip    = proc->entry;
    arch->esp    = USER_STACK;
    arch->eflags = X86_EFLAGS;
#else
    arch->rip    = proc->entry;
    arch->rsp    = USER_STACK;
    arch->rflags = X86_EFLAGS;
#endif

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->arch = arch;
}

/*
 * Sets up init's arguments and stack, in its own address space, before it
 * first runs.
 */
void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    struct pmap *pmap = proc->vm_space.pmap;
    struct thread *thread = (struct thread *) proc->threads.head->value;

    curthread = thread;
    pmap_switch(pmap);

    arch_sys_execve(proc, argc, _argp, envc, _envp);
    curthread = NULL;
}
