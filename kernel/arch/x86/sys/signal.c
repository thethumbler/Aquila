#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <cpu/cpu.h>

void arch_handle_signal(int sig)
{
    uintptr_t handler = cur_proc->signal_handler[sig];
    printk("arch_handle_signal(0x%p)\n", handler);

    x86_proc_t *arch = cur_proc->arch;

    printk("Current regs=0x%p\n", arch->regs);

    uintptr_t esp = 0, ebp = 0;    
    asm("mov %%esp, %0":"=r"(esp)); /* read esp */
    asm("mov %%ebp, %0":"=r"(ebp)); /* read ebp */

    //arch->kstack = esp;
    set_kernel_stack(esp);

    uintptr_t sig_esp = arch->regs->esp;

    /* Push signal number */
    sig_esp -= sizeof(int);
    *(int *) sig_esp = sig;

    /* Push return address */
    sig_esp -= sizeof(uintptr_t);
    *(uintptr_t *) sig_esp = 0x0FFF;
    x86_dump_registers(arch->regs);

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
    x86_jump_user(0, handler, X86_CS, arch->eflags, sig_esp, X86_SS);
}
