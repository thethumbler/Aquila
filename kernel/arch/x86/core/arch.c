#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <mm/mm.h>

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

void switch_pd(uintptr_t pd)
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

	printk("cur %x ret %x\n", ret->cur, ret->new);
	switch_pd(ret->new);
	for(;;);
}