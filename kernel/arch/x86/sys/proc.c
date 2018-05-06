#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

#include "sys.h"

void arch_proc_init(void *d, proc_t *p)
{
    x86_proc_t *parch = kmalloc(sizeof(x86_proc_t));
    x86_thread_t *tarch = kmalloc(sizeof(x86_thread_t));

    memset(parch, 0, sizeof(x86_proc_t));
    memset(tarch, 0, sizeof(x86_thread_t));

    struct arch_binfmt *s = d;

    parch->pd = s->new;

    uintptr_t kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);

    tarch->kstack = kstack_base + KERN_STACK_SIZE;   /* Kernel stack */
    tarch->eip = p->entry;
    tarch->esp = USER_STACK;
    tarch->eflags = X86_EFLAGS;

    p->arch = parch;

    thread_t *t = (thread_t *) p->threads.head->value;
    t->arch = tarch;
}

void arch_proc_kill(proc_t *proc)
{
    x86_proc_t *arch = (x86_proc_t *) proc->arch;

    arch_release_frame(arch->pd);
    kfree(arch);
}
