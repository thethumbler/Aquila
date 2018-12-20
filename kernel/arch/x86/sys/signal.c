#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <cpu/cpu.h>

void arch_handle_signal(int sig)
{
    //printk("arch_handle_signal(sig=%d)\n", sig);
    uintptr_t handler = cur_thread->owner->sigaction[sig].sa_handler;
    //printk("handler=%p\n", handler);

    if (handler == SIG_DFL)
        handler = sig_default_action[sig];

    x86_thread_t *arch = cur_thread->arch;
    //printk("Current regs=%p\n", arch->regs);
    //x86_dump_registers(arch->regs);
    //for (;;);

    switch (handler) {
        case SIGACT_ABORT:
        case SIGACT_TERMINATE:
            cur_thread->owner->exit = _PROC_EXIT(sig, sig);
            proc_kill(cur_thread->owner);
            arch_sleep();
            break;  /* We should never reach this anyway */
    }

    for (;;);



    arch->kstack -= sizeof(struct x86_regs);
    x86_kernel_stack_set(arch->kstack);

#if ARCH_BITS==32
    uintptr_t sig_sp = arch->regs->esp;
#else
    uintptr_t sig_sp = arch->regs->rsp;
#endif

    /* Push signal number */
    sig_sp -= sizeof(int);
    *(int *) sig_sp = sig;

    /* Push return address */
    sig_sp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_sp = 0x0FFF;

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
#if ARCH_BITS==32
    x86_jump_user(0, handler, X86_CS, arch->eflags, sig_sp, X86_SS);
#else
    x86_jump_user(0, handler, X86_CS, arch->rflags, sig_sp, X86_SS);
#endif
}
