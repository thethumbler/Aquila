#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <cpu/cpu.h>

static uintptr_t get_cur_pd()
{
	uintptr_t cur_pd = 0;
	asm("movl %%cr3, %%eax":"=a"(cur_pd));
	return cur_pd;
}

static uintptr_t get_pd()
{
	/* Get a free page frame for storing Page Directory*/
	uintptr_t pd = arch_get_frame();

	/* Copy the Kernel Space to the new Page Directory */
	pmman.memcpypp((void *) pd, (void *) get_cur_pd() + 768 * 4, PAGE_SIZE);

	return pd;
}

static void switch_pd(uintptr_t pd)
{
	asm("mov %%eax, %%cr3;"::"eax"(pd));
}

struct arch_load_elf
{
	uintptr_t cur;
	uintptr_t new;
};

void *arch_load_elf()
{
	struct arch_load_elf *ret = kmalloc(sizeof(*ret));
	ret->cur = get_cur_pd();
	ret->new = get_pd();

	switch_pd(ret->new);
	
	return ret;
}

void arch_init_proc(void *d, proc_t *p, uintptr_t entry)
{
	x86_proc_t *arch = kmalloc(sizeof(x86_proc_t));
	struct arch_load_elf *s = d;

	arch->pd = s->new;
	arch->stat = (x86_stat_t){0};
	
	arch->stat.eip = entry;
	arch->stat.cs  = 0x18 | 3;
	arch->stat.ss  = 0x20 | 3;
	arch->stat.eflags = 0x200;
	arch->stat.esp = USER_STACK;
	
	p->arch = arch;
}

void arch_load_elf_end(void *d)
{
	struct arch_load_elf *p = (struct arch_load_elf *) d;
	switch_pd(p->cur);
	kfree(p);
}

void arch_idle()
{
	for(;;)
		asm volatile("cli; hlt;");
}

void arch_jump_userspace(x86_stat_t *s)
{
	extern void x86_jump_userspace(x86_stat_t s);
	x86_jump_userspace(*s);
}

void arch_switch_process(proc_t *proc)
{
	x86_proc_t *arch = proc->arch;
	printk("Switching %s [%x]\n", proc->name, arch->stat.eip);
	arch->stat.eflags |= 0x200;
	switch_pd(arch->pd);
	arch_jump_userspace(&arch->stat);
}

void *arch_sched()
{
	return NULL;
}

void arch_sched_end(void *p __attribute__((unused)))
{

}

static void arch_sched_handler(regs_t *r)
{
	if(cur_proc)
	{
		x86_proc_t *arch = cur_proc->arch;
		memcpy((void *) &arch->stat, (void *) r, sizeof(x86_stat_t));
	}

	schedule();
}

void arch_sched_init()
{
	x86_pit_setup(1);
	irq_install_handler(PIT_IRQ, arch_sched_handler);
}