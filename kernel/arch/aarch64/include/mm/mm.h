#ifndef _AARCH64_MM_H
#define _AARCH64_MM_H

#include <core/system.h>

/*
 * aarch64's address space: user space is the lower half (TTBR0_EL1, 48
 * bits), and the kernel the upper half (TTBR1_EL1). In the upper half,
 * all physical memory is mapped at ARCH_DMAP_BASE (the direct map), with
 * the kernel in it (linked at _VMA plus its physical address); then come
 * the kernel's heap, its small objects, and the window for devices'
 * registers.
 */
#define ARCH_DMAP_BASE         (0xFFFF000000000000ULL)
#define ARCH_KVMEM_BASE        (0xFFFF800000000000ULL)
#define ARCH_KVMEM_SIZE        (1ULL << 30)                 /* 1 GiB */
#define ARCH_SLAB_BASE         (0xFFFF900000000000ULL)
#define ARCH_SLAB_SIZE         (16ULL << 30)                /* 16 GiB */
#define ARCH_MMIO_BASE         (0xFFFFA00000000000ULL)     /* devices' registers */
#define ARCH_MMIO_SIZE         (64ULL << 30)                /* 64 GiB */

/* How much physical memory the boot tables map (boot/head.S). */
#define ARCH_BOOT_MAPPED       (4ULL << 30)

/* A physical address's kernel address, in the direct map. */
#define PHYS2VIRT(paddr)       ((void *) (ARCH_DMAP_BASE + (uintptr_t) (paddr)))

extern char _VMA; /* the linker script's offset of the kernel's addresses */

/* The kernel address of something in the kernel's image, from its
 * physical address (VMA()), and the other way around (LMA()), as on x86. */
#define __VMA_ADD(obj, op) ((uintptr_t) (obj) op (uintptr_t) &_VMA)
#define __VMA_TYPED(obj, op)                                              \
    __builtin_choose_expr(sizeof(obj) == sizeof(uintptr_t),               \
        (typeof(obj)) __VMA_ADD(obj, op), __VMA_ADD(obj, op))
#define VMA(obj)  __VMA_TYPED(obj, +)
#define LMA(obj)  __VMA_TYPED(obj, -)

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

/** Flushes every translation from this CPU's TLB. */
static inline void tlb_flush(void)
{
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

#include_next <mm/mm.h>

#endif /* ! _AARCH64_MM_H */
