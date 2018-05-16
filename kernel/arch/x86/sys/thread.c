#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

#include "sys.h"

void arch_thread_spawn(thread_t *thread)
{
    x86_thread_t *arch  = thread->arch;
    x86_proc_t   *parch = thread->owner->arch;

    switch_page_directory(parch->pd);
    x86_kernel_stack_set(arch->kstack);

    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
}

void arch_thread_switch(thread_t *thread)
{
    x86_thread_t *arch  = thread->arch;
    x86_proc_t   *parch = thread->owner->arch;

    switch_page_directory(parch->pd);
    x86_kernel_stack_set(arch->kstack);
    disable_fpu();

    if (thread->owner->sig_queue->count) {
        int sig = (int) dequeue(thread->owner->sig_queue);
        arch_handle_signal(sig);
        for (;;);
    }

    x86_goto(arch->eip, arch->ebp, arch->esp);
}

void arch_thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    x86_thread_t *arch = kmalloc(sizeof(x86_thread_t)); 
    memset(arch, 0, sizeof(x86_thread_t));

    arch->kstack = (uintptr_t) kmalloc(KERN_STACK_SIZE) + KERN_STACK_SIZE;
    arch->eip = entry;

    /* Push thread argument */
    PUSH(stack, void *, arg);

    /* Push user entry point */
    PUSH(stack, void *, uentry);

    /* Dummy return address */
    PUSH(stack, void *, 0);

    arch->esp = stack;
    thread->arch = arch;
}

void arch_thread_kill(thread_t *thread)
{
    x86_thread_t *arch = (x86_thread_t *) thread->arch;

    if (thread == cur_thread) {
        /* We don't wanna die here */
        uintptr_t esp = VMA(0x100000);
        x86_kernel_stack_set(esp);
    }

    if (arch->kstack)
        kfree((void *) (arch->kstack - KERN_STACK_SIZE));

    if (arch->fpu_context)
        kfree(arch->fpu_context);

    extern thread_t *last_fpu_thread;
    if (last_fpu_thread == thread)
        last_fpu_thread = NULL;

    kfree(arch);
}

void internal_arch_sleep()
{
    x86_thread_t *arch = cur_thread->arch;
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
