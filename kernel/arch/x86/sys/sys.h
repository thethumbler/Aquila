#include <core/system.h>
#include <mm/mm.h>

/* Common types for sys */
struct arch_load_elf
{
	uintptr_t cur;
	uintptr_t new;
};

/* Common functions for sys */
static inline uintptr_t get_current_page_directory()
{
	uintptr_t cur_pd = 0;
	asm("movl %%cr3, %%eax":"=a"(cur_pd));
	return cur_pd;
}

static inline uintptr_t get_new_page_directory()
{
	/* Get a free page frame for storing Page Directory*/
	uintptr_t pd = arch_get_frame();

	/* Copy the Kernel Space to the new Page Directory */
	void *pd_kernel_dst = (void *) (pd + 768 * 4);
	void *pd_kernel_src = (void * ) (get_current_page_directory() + 768 * 4);
	size_t pd_kernel_size = PAGE_SIZE - 768 * 4;
	pmman.memcpypp(pd_kernel_dst, pd_kernel_src, pd_kernel_size);

	return pd;
}

static inline void switch_page_directory(uintptr_t pd)
{
	asm("mov %%eax, %%cr3;"::"a"(pd));
}