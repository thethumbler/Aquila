#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/lock.h>
#include <sys/uaccess.h>
#include <aquila/syscall.h>
#include <ds/queue.h>
#include <mm/pmap.h>

MALLOC_DEFINE(M_KERN_STACK, "kern-stack", "kernel stack");
MALLOC_DEFINE(M_X86_THREAD, "x86-thread", "x86 thread structure");

#define PUSH(stack, type, value)\
do {\
    (stack) -= sizeof(type);\
    *((type *) (stack)) = (type) (value);\
} while (0)

/** the current thread to user mode, the first time, from its own kernel
 * stack: the kernel lock goes, another CPU may run the thread that was
 * running here (its stack) once it does */
static void __attribute__((noreturn)) thread_spawn(void)
{
    struct x86_thread *arch = curthread->arch;

    kernel_unlock();

#if ARCH_BITS==32
    x86_jump_user(arch->eax, arch->eip, X86_CS, arch->eflags, arch->esp, X86_SS);
#else
    x86_jump_user(arch->rax, arch->rip, X86_CS, arch->rflags, arch->rsp, X86_SS);
#endif
}

void arch_thread_spawn(struct thread *thread)
{
    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);

    /* its first use of the FPU traps (x86_fpu_trap), as after a switch:
     * the registers are another thread's */
    x86_fpu_disable();

    x86_goto((uintptr_t) thread_spawn, 0, arch->kstack);
}

void arch_thread_switch(struct thread *thread)
{
    //printk("[%d:%d] %s: arch_thread_switch(thread=%p)\n", thread->owner->pid, thread->tid, thread->owner->name, thread);

    struct x86_thread *arch  = thread->arch;
    struct pmap *pmap = thread->owner->vm_space.pmap;

    pmap_switch(pmap);

    x86_kernel_stack_set(arch->kstack);
    x86_fpu_disable();

    /* signals are taken on the way back to user mode, not here
     * (x86_return_to_user): the thread resumes where it was */
#if ARCH_BITS==32
    x86_goto(arch->eip, arch->ebp, arch->esp);
#else
    x86_goto(arch->rip, arch->rbp, arch->rsp);
#endif
}

/** the thread starts at `entry' on the user stack `stack', below which its
 * frame is pushed (its argument, the user entry point, a dummy return
 * address): 0, -ENOMEM, -EFAULT if the stack isn't there */
int arch_thread_create(struct thread *thread, uintptr_t stack, uintptr_t entry, uintptr_t uentry, uintptr_t arg)
{
    /* thread->arch is left NULL on failure */
    thread->arch = NULL;

    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    if (!arch)
        return -ENOMEM;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);
    if (!kstack) {
        kfree(arch);
        return -ENOMEM;
    }

    uintptr_t frame[3] = {0, uentry, arg};
    stack -= sizeof(frame);

    if (copyout((void *) stack, frame, sizeof(frame))) {
        kfree(kstack);
        kfree(arch);
        return -EFAULT;
    }

    arch->kstack_base = (uintptr_t) kstack;
    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;

    /* dummy return address */
    PUSH(arch->kstack, void *, 0);

    /* dummy base pointer */
    PUSH(arch->kstack, void *, 0);

#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
#endif

#if ARCH_BITS==32
    arch->esp = stack;
#else
    arch->rsp = stack;
#endif
    thread->arch = arch;
    return 0;
}

/** a kernel thread, which starts at `entry' (a function without arguments
 * that never returns) on its kernel stack once it is switched to */
void arch_kthread_create(struct thread *thread, uintptr_t entry)
{
    /* thread->arch is left NULL on failure */
    thread->arch = NULL;

    struct x86_thread *arch = kmalloc(sizeof(struct x86_thread), &M_X86_THREAD, M_ZERO);
    if (!arch)
        return;

    void *kstack = kmalloc(KERN_STACK_SIZE, &M_KERN_STACK, 0);
    if (!kstack) {
        kfree(arch);
        return;
    }

    arch->kstack_base = (uintptr_t) kstack;
    arch->kstack = (uintptr_t) kstack + KERN_STACK_SIZE;

    /* as if it was called: a return address, none */
    uintptr_t sp = arch->kstack;
    PUSH(sp, void *, 0);

#if ARCH_BITS==32
    arch->eflags = X86_EFLAGS;
    arch->eip = entry;
    arch->esp = sp;
    arch->ebp = 0;
#else
    arch->rflags = X86_EFLAGS;
    arch->rip = entry;
    arch->rsp = sp;
    arch->rbp = 0;
#endif

    thread->arch = arch;
}

void arch_thread_kill(struct thread *thread)
{
    struct x86_thread *arch = (struct x86_thread *) thread->arch;

    /* already killed */
    if (!arch)
        return;

    if (thread == curthread) {
        /* We don't wanna die here */
        uintptr_t esp = VMA(0x100000); /* XXX */
        x86_kernel_stack_set(esp);
    }

    if (arch->kstack_base)
        kfree((void *) arch->kstack_base);

    if (arch->fpu_context)
        kfree(arch->fpu_context);

    /* the FPU's state, wherever it is, isn't its anymore */
    for (int i = 0; i < cpus_count; ++i)
        if (cpus[i].arch.fpu_thread == thread)
            cpus[i].arch.fpu_thread = NULL;

    kfree(arch);
    thread->arch = NULL;
}

/**
 * Where the current thread resumes when it is switched back to (x86_goto):
 * x86_sleep and x86_preempt record it, its registers on its kernel stack
 */
void x86_thread_saved(uintptr_t ip, uintptr_t sp, uintptr_t bp)
{
    struct x86_thread *arch = curthread->arch;

    x86_fpu_switch_out();

#if ARCH_BITS==32
    arch->eip = ip;
    arch->esp = sp;
    arch->ebp = bp;
#else
    arch->rip = ip;
    arch->rsp = sp;
    arch->rbp = bp;
#endif
}
