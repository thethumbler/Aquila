#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <mm/mm.h>

#include "sys.h"

static void x86_sched_handler(regs_t *r __unused)
{
	if(!kidle)
	{
		x86_proc_t *arch = (x86_proc_t *) cur_proc->arch;
		extern uintptr_t x86_read_eip();

		uintptr_t eip = 0, esp = 0, ebp = 0;	
		asm("mov %%esp, %0":"=r"(esp));	/* read esp */
		asm("mov %%ebp, %0":"=r"(ebp));	/* read ebp */
		eip = x86_read_eip();

		if(eip == (uintptr_t) -1)	/* Done switching */
		{
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
	x86_pit_setup(10);
	irq_install_handler(PIT_IRQ, x86_sched_handler);
}

void arch_idle()
{
	for(;;)
		asm volatile("sti; hlt; cli;");
}