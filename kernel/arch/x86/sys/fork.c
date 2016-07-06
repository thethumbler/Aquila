#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include "sys.h"

void arch_sys_fork(proc_t *proc)
{
	x86_proc_t *orig_arch = cur_proc->arch;
	x86_proc_t *fork_arch = kmalloc(sizeof(x86_proc_t));

	uintptr_t cur_proc_pd = get_current_page_directory();
	uintptr_t new_proc_pd = get_new_page_directory();
	switch_page_directory(new_proc_pd);

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

	/* Setup kstack and fork state */
	uintptr_t fork_kstack_base = (uintptr_t) kmalloc(KERN_STACK_SIZE);
	fork_arch->kstack = fork_kstack_base + KERN_STACK_SIZE;

	/* Copy registers */
	void *fork_regs = (void *)(fork_arch->kstack - sizeof(regs_t));
	memcpy(fork_regs, orig_arch->regs, sizeof(regs_t));
	fork_arch->regs = fork_regs;
	
	extern void x86_fork_return();
	fork_arch->eip = (uintptr_t) x86_fork_return;
	fork_arch->esp = (uintptr_t) fork_regs;
	//fork_arch->ebp = orig_arch->regs->ebp;
	//fork_arch->eflags = orig_arch->regs->eflags;

	/* Copy stack */
	pmman.map(USER_STACK_BASE, USER_STACK_SIZE, URWX);
	

#define STACK_BUF	(1024UL*1024)

	/* Copying stack in SATCK_BUF blocks */
	void *stack_buf = kmalloc(STACK_BUF);

	for(uint32_t i = 0; i < USER_STACK_SIZE/STACK_BUF; ++i)
	{
		switch_page_directory(cur_proc_pd);
		memcpy(stack_buf, (char *) USER_STACK_BASE + i * STACK_BUF, STACK_BUF);
		switch_page_directory(new_proc_pd);
		memcpy((char *) USER_STACK_BASE + i * STACK_BUF, stack_buf, STACK_BUF);
		switch_page_directory(cur_proc_pd);
	}

	kfree(stack_buf);

	fork_arch->pd = new_proc_pd;
	proc->arch = fork_arch;
}