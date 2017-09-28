#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <mm/mm.h>

#include "sys.h"

static void x86_sched_handler(regs_t *r __unused) 
{
    //extern queue_t *procs;
    //printk("----- procs dump -----\n");
    //forlinked (node, procs->head, node->next) {
    //    proc_t *proc = node->value;
    //    printk("(%d, %s)\n", proc->pid, proc->name);
    //}
    //printk("----------------------\n");

    if (!kidle) {
        x86_proc_t *arch = (x86_proc_t *) cur_proc->arch;
        extern uintptr_t x86_read_eip();

        volatile uintptr_t eip = 0, esp = 0, ebp = 0;    
        asm volatile("mov %%esp, %0":"=r"(esp)); /* read esp */
        asm volatile("mov %%ebp, %0":"=r"(ebp)); /* read ebp */
        eip = x86_read_eip();

        if (eip == (uintptr_t) -1) {    /* Done switching */
            return;
        }

        arch->eip = eip;
        arch->esp = esp;
        arch->ebp = ebp;
    }

    schedule();
}

void arch_sched_init()
{
    //pit_setup(10);
    irq_install_handler(PIT_IRQ, x86_sched_handler);
}

void __arch_idle()
{
    for (;;)
        asm volatile("sti; hlt; cli;");
}

void arch_idle()
{
    cur_proc = NULL;
    uintptr_t esp = VMA(0x100000);
    set_kernel_stack(esp);
    extern void x86_goto(uintptr_t eip, uintptr_t ebp, uintptr_t esp) __attribute__((noreturn));
    x86_goto((uintptr_t) __arch_idle, esp, esp);
}
