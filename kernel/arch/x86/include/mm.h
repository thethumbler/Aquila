#ifndef _X86_MM_H
#define _X86_MM_H

#include <core/system.h>
#include <cpu/cpu.h>

#define NR_PAGE_SIZE    2
#define KERNEL_HEAP_SIZE    (8 * 1024  * 1024)  /* 8 MiB */
#define ARCH_VMM_BASE        (0xD0000000UL)
#define ARCH_VMM_NODES_SIZE  (0x00100000UL)

extern char _VMA; /* Must be defined in linker script */
#define VMA(obj)  ((typeof((obj)))((uintptr_t)(void*)&_VMA + (uintptr_t)(void*)(obj)))
#define LMA(obj)  ((typeof((obj)))((uintptr_t)(void*)(obj)) - (uintptr_t)(void*)&_VMA)

static inline void TLB_flush()
{
    //asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3":::"eax");
    write_cr3(read_cr3());
}

void pmm_setup();
void arch_pmm_setup();
void vmm_setup();
uintptr_t arch_get_frame();
uintptr_t arch_get_frame_no_clr();
void arch_release_frame(uintptr_t);

#endif /* !_X86_MM_H */
