#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <cpu/cpu.h>

void arch_handle_signal(int sig)
{
    uintptr_t handler = cur_proc->signal_handler[sig];
    if (!handler) handler = sig_default_action[sig];

    switch (handler) {
        case SIGACT_ABORT:
        case SIGACT_TERMINATE:
            kill_proc(cur_proc);
            kernel_idle();
            break;  /* We should never reach this anyway */
    }


    x86_proc_t *arch = cur_proc->arch;

    //printk("Current regs=%p\n", arch->regs);

    arch->kstack -= sizeof(regs_t);
    set_kernel_stack(arch->kstack);

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
