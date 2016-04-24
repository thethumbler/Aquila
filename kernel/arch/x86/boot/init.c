#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <boot/multiboot.h>
#include <boot/boot.h>

/* FIXME */
char scratch[8192 * 1024] __attribute__((aligned(0x1000)));	/* 8 MiB scratch area */

void *kernel_heap = (void*)scratch;
static void *heap_alloc(size_t size, size_t align)
{
	void *ret = (void*)((uintptr_t)(kernel_heap + align - 1) & (~(align - 1)));
	kernel_heap = (void*)((uintptr_t)ret + size);

	memset(ret, 0, size);	/* We always clear the allocated area */

	return ret;
}

extern char kernel_end; /* Defined in linker script */

/* Page structure for BSP */
volatile uint32_t *_BSP_PD  = NULL;
volatile uint32_t *_BSP_LPT = NULL;

void switch_to_higher_half()
{
	_BSP_PD = heap_alloc(PAGE_SIZE, PAGE_SIZE);

	uint32_t page_tables = ((uintptr_t)(&kernel_end) + TABLE_MASK) / TABLE_SIZE;
	uint32_t *BSP_PT = heap_alloc(page_tables * PAGE_SIZE, PAGE_SIZE);

	uint32_t i;
	for(i = 0; i < page_tables * 1024; ++i)
		BSP_PT[i] = (0x1000 * i) | 3;

	for(i = 0; i < page_tables; ++i)
	{
		_BSP_PD[i]       = (uint32_t)((uintptr_t)BSP_PT + i * PAGE_SIZE) | 3;
		_BSP_PD[768 + i] = (uint32_t)((uintptr_t)BSP_PT + i * PAGE_SIZE) | 3;
	}


	_BSP_LPT = heap_alloc(PAGE_SIZE, PAGE_SIZE);
	_BSP_PD[1023] = (uint32_t)_BSP_LPT | 3;

	asm volatile("mov %%eax, %%cr3;"::"a"(_BSP_PD));
	asm volatile("mov %%cr4, %%eax; or %0, %%eax; mov %%eax, %%cr4;"::"g"(0x10));
	asm volatile("mov %%cr0, %%eax; or %0, %%eax; mov %%eax, %%cr0;"::"g"(0x80000000));
}

void early_init()
{
	/* We assume that GrUB loaded a valid GDT */
	/* Then we map the kernel to the higher half */
	switch_to_higher_half();
	/* Now we make SP in the higher half */
	asm volatile("addl %0, %%esp"::"g"(VMA((uintptr_t)0)):"esp");
	/* Ready to get out of here */
	extern void cpu_init();
	cpu_init();
	for(;;);
}
