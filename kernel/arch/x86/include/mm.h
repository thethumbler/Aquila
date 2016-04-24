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

#endif /* !_X86_MM_H */
