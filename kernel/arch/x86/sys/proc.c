#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

#include "sys.h"

void arch_proc_spawn(proc_t *proc __unused)
{
#if 0
    x86_proc_t *arch = proc->arch;
    printk("[%d] %s: Spawning [IP: %p, SP: %p, F: 0x%x, KSTACK: %p]\n", proc->pid, proc->name, arch->eip, arch->esp, arch->eflags, arch->kstack);
    
    switch_page_directory(arch->pd);

    set_kernel_stack(arch->kstack);

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
#endif
}

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

#if 0
void arch_proc_switch(proc_t *proc)
{
    x86_proc_t *arch = proc->arch;
    //printk("[%d] %s: Switching [KSTACK: %p, EIP: %p, ESP: %p]\n", proc->pid, proc->name, arch->kstack, arch->eip, arch->esp);

    switch_page_directory(arch->pd);
    set_kernel_stack(arch->kstack);
    disable_fpu();

    if (proc->signals_queue->count) {
        //printk("There are %d pending signals\n", proc->signals_queue->count);
        int sig = (int) dequeue(proc->signals_queue);
        arch_handle_signal(sig);
        for (;;);
    }

    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto(arch->eip, arch->ebp, arch->esp);
}
#endif

void arch_proc_kill(proc_t *proc __unused)
{
#if 0
    x86_proc_t *arch = (x86_proc_t *) proc->arch;

    if (arch->kstack)
        kfree((void *) (arch->kstack - KERN_STACK_SIZE));

    if (arch->fpu_context)
        kfree(arch->fpu_context);

    extern proc_t *last_fpu_proc;
    if (last_fpu_proc == proc)
        last_fpu_proc = NULL;

    arch_release_frame(arch->pd);
    kfree(arch);
#endif
}

#if 0
void arch_sleep()
{
    extern void x86_sleep();
    x86_sleep();
}

void internal_arch_sleep()
{
    x86_proc_t *arch = cur_proc->arch;
    extern uintptr_t x86_read_eip();

    uintptr_t eip = 0, esp = 0, ebp = 0;    
    asm("mov %%esp, %0":"=r"(esp)); /* read esp */
    asm("mov %%ebp, %0":"=r"(ebp)); /* read ebp */
    eip = x86_read_eip();

    if (eip == (uintptr_t) -1) {  /* Done switching */
        return;
    }

    arch->eip = eip;
    arch->esp = esp;
    arch->ebp = ebp;
    kernel_idle();
}
#endif
