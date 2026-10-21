#ifndef _I386_MM_H
#define _I386_MM_H

#include <core/system.h>
#include <cpu/cpu.h>

#define NR_PAGE_SIZE           2

/* the physical memory the boot code maps at the kernel's addresses (the
 * kernel, then what mm_setup() needs at boot: pages[] is 16 bytes a
 * page on i386, 32 on x86-64) */
#if ARCH_BITS==32
#define ARCH_BOOT_MAPPED       (128UL * 1024 * 1024)        /* 128 MiB */
#else
#define ARCH_BOOT_MAPPED       (1024UL * 1024 * 1024)       /* 1 GiB */
#endif

#if ARCH_BITS==32
#define ARCH_KVMEM_BASE        (0xD0000000UL)
#define ARCH_KVMEM_NODES_SIZE  (0x00100000UL)

/* fixed kernel mappings of devices */
#define ARCH_VESA_BASE         (0xE0000000UL)
#define ARCH_EARLYFB_BASE      (0xE8000000UL)
#define ARCH_ACPI_BASE         (0xF0000000UL)

/* the pages of the kernel's small objects (mm/slab.c) */
#define ARCH_SLAB_BASE         (0xF1000000UL)
#define ARCH_SLAB_SIZE         (0x0E000000UL)               /* 224 MiB */
#else
/*
 * x86-64: user space is the lower half; in the upper half all physical
 * memory is mapped at ARCH_DMAP_BASE (the direct map), then the devices,
 * the kernel's heap, and the kernel at -2 GiB (_VMA)
 */
#define ARCH_DMAP_BASE         (0xFFFF800000000000ULL)
#define ARCH_VESA_BASE         (0xFFFFC00000000000ULL)
#define ARCH_EARLYFB_BASE      (0xFFFFC00100000000ULL)
#define ARCH_ACPI_BASE         (0xFFFFC00200000000ULL)
#define ARCH_KVMEM_BASE        (0xFFFFD00000000000ULL)
#define ARCH_KVMEM_NODES_SIZE  (0x00100000ULL)
#define ARCH_SLAB_BASE         (0xFFFFE00000000000ULL)
#define ARCH_SLAB_SIZE         (16ULL << 30)                /* 16 GiB */

/* a physical address in the direct map */
#define PHYS2VIRT(paddr)       ((void *) (ARCH_DMAP_BASE + (uintptr_t) (paddr)))
#endif

extern char _VMA; /* Must be defined in linker script */

/* the kernel's address of something in the kernel's image (or in the
 * memory below it, mapped with it), and the other way around: a pointer
 * stays a pointer, a narrower integer (a 32-bit physical address) becomes
 * a uintptr_t */
#define __VMA_ADD(obj, op) ((uintptr_t) (obj) op (uintptr_t) &_VMA)
#define __VMA_TYPED(obj, op)                                              \
    __builtin_choose_expr(sizeof(obj) == sizeof(uintptr_t),               \
        (typeof(obj)) __VMA_ADD(obj, op), __VMA_ADD(obj, op))
#define VMA(obj)  __VMA_TYPED(obj, +)
#define LMA(obj)  __VMA_TYPED(obj, -)

static inline void tlb_flush(void)
{
    write_cr3(read_cr3());
}

#if ARCH_BITS==32
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;
#else
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
#endif

void arch_mm_page_fault(vaddr_t vaddr, int err);

#include_next <mm/mm.h>

#endif /* ! _I386_MM_H */
