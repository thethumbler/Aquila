#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

void arch_proc_init(struct proc *proc)
{
    //struct pmap *pmap = arch_pmap_create();
    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, 0);
    memset(arch, 0, sizeof(struct x86_thread));

    //struct arch_binfmt *s = d;
    //pmap->map = s->new_map;

    uintptr_t kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);

    arch->kstack = kstack_base + KERN_STACK_SIZE;   /* Kernel stack */
#if ARCH_BITS==32
    arch->eip    = proc->entry;
    arch->esp    = USER_STACK;
    arch->eflags = X86_EFLAGS;
#else
    arch->rip    = proc->entry;
    arch->rsp    = USER_STACK;
    arch->rflags = X86_EFLAGS;
#endif

    //p->vm_space.pmap = pmap;

    struct thread *thread = (struct thread *) proc->threads.head->value;
    thread->arch = arch;
}

void arch_init_execve(struct proc *proc, int argc, char * const _argp[], int envc, char * const _envp[])
{
    printk("arch_init_execve(proc=%p, argc=%d, _argp=%p, envc=%d, _envp=%p)\n", proc, argc, _argp, envc, _argp);

    struct pmap *pmap = proc->vm_space.pmap;
    struct thread *thread = (struct thread *) proc->threads.head->value;

    cur_thread = thread;
    //arch_switch_mapping(pmap->map);
    arch_pmap_switch(pmap);

    arch_sys_execve(proc, argc, _argp, envc, _envp);
    cur_thread = NULL;
}
