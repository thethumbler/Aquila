#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

void arch_proc_init(void *d, struct proc *p)
{
    struct x86_proc *parch = kmalloc(sizeof(struct x86_proc));
    struct x86_thread *tarch = kmalloc(sizeof(struct x86_thread));

    memset(parch, 0, sizeof(struct x86_proc));
    memset(tarch, 0, sizeof(struct x86_thread));

    struct arch_binfmt *s = d;

    parch->map = s->new_map;

    uintptr_t kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);

    tarch->kstack = kstack_base + KERN_STACK_SIZE;   /* Kernel stack */
#if ARCH_BITS==32
    tarch->eip = p->entry;
    tarch->esp = USER_STACK;
    tarch->eflags = X86_EFLAGS;
#else
    tarch->rip = p->entry;
    tarch->rsp = USER_STACK;
    tarch->rflags = X86_EFLAGS;
#endif

    p->arch = parch;

    struct thread *t = (struct thread *) p->threads.head->value;
    t->arch = tarch;
}

void arch_proc_kill(struct proc *proc)
{
    struct x86_proc *arch = (struct x86_proc *) proc->arch;

    arch_release_frame(arch->map);
    kfree(arch);
}

void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    //printk("arch_init_execve(proc=%p, argc=%d, _argp=%p, envc=%d, _envp=%p)\n", proc, argc, _argp, envc, _argp);

    struct thread *thread = (struct thread *) proc->threads.head->value;
    struct x86_proc *arch  = thread->owner->arch;

    cur_thread = thread;
    arch_switch_mapping(arch->map);
    arch_sys_execve(proc, argc, _argp, envc, _envp);
    cur_thread = NULL;
}
