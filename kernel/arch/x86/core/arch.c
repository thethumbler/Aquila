#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <mm/mm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syscall.h>
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
	void *pd_kernel_dst = (void *) (pd + 768 * 4);
	void *pd_kernel_src = (void * ) (get_cur_pd() + 768 * 4);
	size_t pd_kernel_size = PAGE_SIZE - 768 * 4;
	pmman.memcpypp(pd_kernel_dst, pd_kernel_src, pd_kernel_size);

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

void arch_kill_process(proc_t *proc)
{
	x86_proc_t *arch = proc->arch;
	arch_release_frame(arch->pd);
	kfree(arch);
}

void arch_idle()
{
	for(;;)
		asm volatile("sti; hlt; cli;");
}

void arch_jump_userspace(x86_stat_t *s)
{
	extern void x86_jump_userspace(x86_stat_t);
	x86_jump_userspace(*s);
}

void arch_switch_process(proc_t *proc)
{
	x86_proc_t *arch = proc->arch;
	printk("Switching %s (%d) [IP: %x]\n", proc->name, proc->pid, arch->stat.eip);
	arch->stat.eflags |= 0x200;	/* Make sure interrupts are enabled */
	switch_pd(arch->pd);
	arch_jump_userspace(&arch->stat);
}

void arch_sched()
{
	/* Literally nothing */
}

static void arch_sched_handler(regs_t *r)
{
	if(!kidle && cur_proc)
	{
		x86_proc_t *arch = cur_proc->arch;
		memcpy((void *) &arch->stat, (void *) r, sizeof(x86_stat_t));
	}

	schedule();
}

void arch_sched_init()
{
	x86_pit_setup(10);
	irq_install_handler(PIT_IRQ, arch_sched_handler);
}

void arch_syscall(regs_t *r)
{
	x86_proc_t *arch = cur_proc->arch;
	memcpy((void *) &arch->stat, (void *) r, sizeof(x86_stat_t));

	void (*syscall)() = syscall_table[r->eax];
	syscall(r->ebx, r->ecx, r->edx);

	arch_jump_userspace(&arch->stat); /* Return to userspace */
}

void arch_sys_fork(proc_t *proc)
{
	x86_proc_t *orig_arch = cur_proc->arch;
	x86_proc_t *fork_arch = kmalloc(sizeof(x86_proc_t));

	memcpy(&fork_arch->stat, &orig_arch->stat, sizeof(x86_stat_t));
	uintptr_t cur_proc_pd = get_cur_pd();
	uintptr_t new_proc_pd = get_pd();
	switch_pd(new_proc_pd);

	int tables_count = ((uintptr_t) proc->heap + TABLE_MASK)/TABLE_SIZE;
	uint32_t *tables_buf = kmalloc(tables_count * sizeof(uint32_t));
	uint32_t *pages_buf  = kmalloc(tables_count * PAGE_SIZE);
	memset(tables_buf, 0, tables_count * sizeof(uint32_t));
	memset(pages_buf,  0, tables_count * PAGE_SIZE);

	pmman.memcpypv(tables_buf, (void *) cur_proc_pd, tables_count * sizeof(uint32_t));

	for(int i = 0; i < tables_count; ++i)
		if(tables_buf[i] != 0)
		{
			uintptr_t page = (uintptr_t) pages_buf + i * PAGE_SIZE;
			pmman.memcpypv((void *) page, (void *) (tables_buf[i] & ~PAGE_MASK), PAGE_SIZE);
		}

	
	for(uint32_t i = 0; i < tables_count * 1024UL; ++i)
		if(pages_buf[i] != 0)
		{
			pmman.map(i * PAGE_SIZE, PAGE_SIZE, URWX);
			pmman.memcpypv((void*) (i * PAGE_SIZE), (void*) (pages_buf[i] & ~PAGE_MASK), PAGE_SIZE);
		}
	
	kfree(tables_buf);
	kfree(pages_buf);

	/* Copy stack */
	pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URWX);
	

#define STACK_BUF	(1024UL*1024)

	/* Copying stack in SATCK_BUF blocks */
	void *stack_buf = kmalloc(STACK_BUF);
	printk("stack_buf %x\n");

	for(uint32_t i = 0; i < USER_STACK_SIZE/STACK_BUF; ++i)
	{
		switch_pd(cur_proc_pd);
		memcpy(stack_buf, (void *) USER_STACK_BASE + i * STACK_BUF, STACK_BUF);
		switch_pd(new_proc_pd);
		memcpy((void *) USER_STACK_BASE + i * STACK_BUF, stack_buf, STACK_BUF);
		switch_pd(cur_proc_pd);
	}

	kfree(stack_buf);

	fork_arch->pd = new_proc_pd;
	proc->arch = fork_arch;
}

void arch_user_return(proc_t *proc, uintptr_t val)
{
	x86_proc_t *arch = proc->arch;
	arch->stat.eax = val;
}