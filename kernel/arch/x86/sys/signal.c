#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <cpu/cpu.h>

void arch_handle_signal(int sig)
{
    printk("arch_handle_signal(sig=%d)\n", sig);
    uintptr_t handler = cur_thread->owner->sigaction[sig].sa_handler;
    printk("handler=%p\n", handler);

    if (handler == SIG_DFL)
        handler = sig_default_action[sig];

    switch (handler) {
        case SIGACT_ABORT:
        case SIGACT_TERMINATE:
            cur_thread->owner->exit = _PROC_EXIT(sig, sig);
            proc_kill(cur_thread->owner);
            arch_sleep();
            break;  /* We should never reach this anyway */
    }


    x86_thread_t *arch = cur_thread->arch;

    //printk("Current regs=%p\n", arch->regs);

    arch->kstack -= sizeof(struct x86_regs);
    x86_kernel_stack_set(arch->kstack);

    uintptr_t sig_esp = arch->regs->esp;

    /* Push signal number */
    sig_esp -= sizeof(int);
    *(int *) sig_esp = sig;

    /* Push return address */
    sig_esp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_esp = 0x0FFF;

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
    x86_jump_user(0, handler, X86_CS, arch->eflags, sig_esp, X86_SS);
}
