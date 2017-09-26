#include <core/system.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <sys/proc.h>

/* Common types for sys */
struct arch_load_elf
{
	uintptr_t cur;
	uintptr_t new;
};

/* Common functions for sys */
static inline uintptr_t get_current_page_directory()
{
    return read_cr3() & ~PAGE_MASK;
}

static inline uintptr_t get_new_page_directory()
{
	/* Get a free page frame for storing Page Directory */
	return arch_get_frame();
}

static inline void switch_page_directory(uintptr_t pd)
{
    pmman.switch_mapping(pd);
}
