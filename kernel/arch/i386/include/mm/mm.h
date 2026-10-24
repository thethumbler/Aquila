#ifndef _I386_MM_H
#define _I386_MM_H

#include <core/system.h>
#include <cpu/cpu.h>

/* x86: the layout of the address space, and the physical and virtual
 * address types. */

#define NR_PAGE_SIZE           2

#if ARCH_BITS==32
/*
 * i386: user space is below 3 GiB. Above it, RAM is mapped from
 * 0xC0000000 (the direct map, with the kernel at 1 MiB in it: _VMA), 768
 * MiB of it at most (the rest isn't used). Then come the kernel's heap,
 * its small objects, the devices, and a window for the memory past the
 * direct map that the kernel maps (the boot loader's modules, ACPI's
 * tables, the devices' registers).
 */
#define ARCH_DMAP_BASE         (0xC0000000UL)
#define ARCH_DMAP_SIZE         (0x30000000UL)               /* 768 MiB */
#define ARCH_RAM_MAX           ARCH_DMAP_SIZE
#define ARCH_KVMEM_BASE        (0xF0000000UL)
#define ARCH_KVMEM_SIZE        (0x04000000UL)               /* 64 MiB */
#define ARCH_SLAB_BASE         (0xF4000000UL)
#define ARCH_SLAB_SIZE         (0x04000000UL)               /* 64 MiB */
#define ARCH_VESA_BASE         (0xF8000000UL)
#define ARCH_EARLYFB_BASE      (0xF9000000UL)
#define ARCH_ACPI_BASE         (0xFA000000UL)
#define ARCH_PHYS_WINDOW_BASE  (0xFA400000UL)
#define ARCH_PHYS_WINDOW_SIZE  (0x05800000UL)               /* 88 MiB */

/* the boot code maps all of the direct map */
#define ARCH_BOOT_MAPPED       ARCH_DMAP_SIZE
#else
/*
 * x86_64: user space is the lower half. In the upper half, all of
 * physical memory is mapped at ARCH_DMAP_BASE (the direct map); then come
 * the devices, the kernel's heap, and the kernel itself, at -2 GiB
 * (_VMA).
 */
#define ARCH_DMAP_BASE         (0xFFFF800000000000ULL)
#define ARCH_VESA_BASE         (0xFFFFC00000000000ULL)
#define ARCH_EARLYFB_BASE      (0xFFFFC00100000000ULL)
#define ARCH_ACPI_BASE         (0xFFFFC00200000000ULL)
#define ARCH_MMIO_BASE         (0xFFFFC00300000000ULL)     /* devices' registers */
#define ARCH_MMIO_SIZE         (64ULL << 30)                /* 64 GiB */
#define ARCH_KVMEM_BASE        (0xFFFFD00000000000ULL)
#define ARCH_KVMEM_SIZE        (1ULL << 30)                 /* 1 GiB */
#define ARCH_SLAB_BASE         (0xFFFFE00000000000ULL)
#define ARCH_SLAB_SIZE         (16ULL << 30)                /* 16 GiB */

/* The boot code maps the kernel, and what follows it of the first GiB, at
 * -2 GiB (mm_setup() puts pages[], 32 bytes a page, there). */
#define ARCH_BOOT_MAPPED       (1024UL * 1024 * 1024)       /* 1 GiB */
#endif

/* a physical address in the direct map */
#define PHYS2VIRT(paddr)       ((void *) (ARCH_DMAP_BASE + (uintptr_t) (paddr)))

extern char _VMA; /* the kernel's virtual base, from the linker script */

/* VMA() is the kernel's address of a physical address in the kernel's
 * image (or in the memory below it, mapped with it), and LMA() the other
 * way around. A pointer stays a pointer; a narrower integer (a 32-bit
 * physical address) becomes a uintptr_t. */
#define __VMA_ADD(obj, op) ((uintptr_t) (obj) op (uintptr_t) &_VMA)
#define __VMA_TYPED(obj, op)                                              \
    __builtin_choose_expr(sizeof(obj) == sizeof(uintptr_t),               \
        (typeof(obj)) __VMA_ADD(obj, op), __VMA_ADD(obj, op))
#define VMA(obj)  __VMA_TYPED(obj, +)
#define LMA(obj)  __VMA_TYPED(obj, -)

/* Flushes the running CPU's TLB (but its global pages). */
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

/**
 * Handles a page fault: turns the CPU's error code into the kernel's
 * `PF_*` flags, and calls `mm_page_fault()` (mm/i386.c, mm/x86_64.c).
 *
 * @param vaddr  the address that faulted (CR2)
 * @param err    the CPU's error code
 * @return 0 if the fault is resolved, or `-EFAULT` if it isn't
 */
int  arch_mm_page_fault(vaddr_t vaddr, int err);

#include_next <mm/mm.h>

#endif /* ! _I386_MM_H */
