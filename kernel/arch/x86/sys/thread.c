#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <ds/queue.h>

#define PUSH(stack, type, value)\
do {\
    (stack) -= sizeof(type);\
    *((type *) (stack)) = (type) (value);\
} while (0)

void arch_thread_spawn(thread_t *thread)
{
    x86_thread_t *arch  = thread->arch;
    x86_proc_t   *parch = thread->owner->arch;

    arch_switch_mapping(parch->map);
    x86_kernel_stack_set(arch->kstack);

#if ARCH_BITS==32
    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
#else
    x86_jump_user(arch->rax, arch->rip, X86_CS, arch->rflags, arch->rsp, X86_SS);
#endif
}

void arch_thread_switch(thread_t *thread)
{
    x86_thread_t *arch  = thread->arch;
    x86_proc_t   *parch = thread->owner->arch;

    arch_switch_mapping(parch->map);
    x86_kernel_stack_set(arch->kstack);
    disable_fpu();

    if (thread->owner->sig_queue->count) {
        int sig = (int)(intptr_t) dequeue(thread->owner->sig_queue);
        arch_handle_signal(sig);
        for (;;);
    }

#if ARCH_BITS==32
    x86_goto(arch->eip, arch->ebp, arch->esp);
#else
    x86_goto(arch->rip, arch->rbp, arch->rsp);
#endif
}

void arch_thread_create(thread_t *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    x86_thread_t *arch = kmalloc(sizeof(x86_thread_t)); 
    memset(arch, 0, sizeof(x86_thread_t));

    arch->kstack = (uintptr_t) kmalloc(KERN_STACK_SIZE) + KERN_STACK_SIZE;
#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
#endif

    /* Push thread argument */
    PUSH(stack, void *, arg);

    /* Push user entry point */
    PUSH(stack, void *, uentry);

    /* Dummy return address */
    PUSH(stack, void *, 0);

#if ARCH_BITS==32
    arch->esp = stack;
#else
    arch->rsp = stack;
#endif
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
    extern uintptr_t x86_read_ip();

    uintptr_t ip = 0, sp = 0, bp = 0;    
#if ARCH_BITS==32
    asm("mov %%esp, %0":"=r"(sp)); /* read esp */
    asm("mov %%ebp, %0":"=r"(bp)); /* read ebp */
#else
    asm("mov %%rsp, %0":"=r"(sp)); /* read rsp */
    asm("mov %%rbp, %0":"=r"(bp)); /* read rbp */
#endif
    ip = x86_read_ip();

    if (ip == (uintptr_t) -1) {  /* Done switching */
        return;
    }

#if ARCH_BITS==32
    arch->eip = ip;
    arch->esp = sp;
    arch->ebp = bp;
#else
    arch->rip = ip;
    arch->rsp = sp;
    arch->rbp = bp;
#endif
    kernel_idle();
}
