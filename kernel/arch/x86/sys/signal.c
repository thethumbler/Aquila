#include <core/system.h>
#include <core/arch.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/sched.h>

void arch_handle_signal(int sig)
{
    uintptr_t handler = cur_proc->signal_handler[sig];
    printk("arch_handle_signal(0x%p)\n", handler);

    x86_proc_t *arch = cur_proc->arch;

    printk("Current regs=0x%p\n", arch->regs);

    uintptr_t esp = 0, ebp = 0;    
    asm("mov %%esp, %0":"=r"(esp)); /* read esp */
    asm("mov %%ebp, %0":"=r"(ebp)); /* read ebp */

    set_kernel_stack(esp);

    /* Push signal number */
    arch->esp -= sizeof(int);
    *(int *) arch->esp = sig;

    /* Push return address */
    arch->esp -= sizeof(uintptr_t);
    *(uintptr_t *) arch->esp = 0x0FFF;
    printk("arch->esp = 0x%p\n", arch->esp);

    extern void x86_jump_user(uintptr_t eax, uintptr_t eip, uintptr_t cs, uintptr_t eflags, uintptr_t esp, uintptr_t ss) __attribute__((noreturn));
    x86_jump_user(arch->eax, handler, X86_CS, arch->eflags, arch->esp, X86_SS);
}
