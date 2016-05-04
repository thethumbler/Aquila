#ifndef _X86_MM_H
#define _X86_MM_H

#include <core/system.h>

extern char _VMA; /* Must be defined in linker script */
#define VMA(obj)  ((typeof((obj)))((uintptr_t)(void*)&_VMA + (uintptr_t)(void*)(obj)))
#define LMA(obj)  ((typeof((obj)))((uintptr_t)(void*)(obj)) - (uintptr_t)(void*)&_VMA)

static inline void TLB_flush()
{
	asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3":::"eax");
}

void x86_mm_setup();
void x86_vmm_setup();
void *kmalloc(size_t);
void kfree(void*);
uintptr_t arch_get_frame();
uintptr_t arch_get_frame_no_clr();
void arch_release_frame(uintptr_t);

#endif /* !_X86_MM_H */
