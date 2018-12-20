#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

void arch_proc_init(void *d, proc_t *p)
{
    x86_proc_t *parch = kmalloc(sizeof(x86_proc_t));
    x86_thread_t *tarch = kmalloc(sizeof(x86_thread_t));

    memset(parch, 0, sizeof(x86_proc_t));
    memset(tarch, 0, sizeof(x86_thread_t));

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

    thread_t *t = (thread_t *) p->threads.head->value;
    t->arch = tarch;
}

void arch_proc_kill(proc_t *proc)
{
    x86_proc_t *arch = (x86_proc_t *) proc->arch;

    arch_release_frame(arch->map);
    kfree(arch);
}

void arch_init_execve(proc_t *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    thread_t     *thread = (thread_t *) proc->threads.head->value;
    x86_proc_t   *arch  = thread->owner->arch;

    cur_thread = thread;
    arch_switch_mapping(arch->map);
    arch_sys_execve(proc, argc, _argp, envc, _envp);
    cur_thread = NULL;
}
