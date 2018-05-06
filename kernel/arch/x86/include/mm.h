#ifndef _X86_MM_H
#define _X86_MM_H

#include <core/system.h>
#include <cpu/cpu.h>

#define NR_PAGE_SIZE           2
#define KERNEL_HEAP_SIZE       (8 * 1024 * 1024)  /* 8 MiB */
#define ARCH_KVMEM_BASE        (0xD0000000UL)
#define ARCH_KVMEM_NODES_SIZE  (0x00100000UL)

extern char _VMA; /* Must be defined in linker script */
#define VMA(obj)  ((typeof((obj)))((uintptr_t)(void*)&_VMA + (uintptr_t)(void*)(obj)))
#define LMA(obj)  ((typeof((obj)))((uintptr_t)(void*)(obj)) - (uintptr_t)(void*)&_VMA)

static inline void tlb_flush()
{
    write_cr3(read_cr3());
}

typedef uint32_t paddr_t;
paddr_t arch_get_frame();
void arch_release_frame(paddr_t paddr);
void arch_switch_directory(paddr_t new_dir);
void arch_mm_fork(paddr_t base, paddr_t fork);

#endif /* ! _X86_MM_H */
