/**********************************************************************
 *                  Early initalization of the kernel
 *                   (Switch to higher half kernel)
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <boot/multiboot.h>
#include <boot/boot.h>

/* Minimalistic Paging structure for BSP */
#if ARCH_BITS==32
volatile uint32_t _BSP_PD[1024]  __aligned(PAGE_SIZE);
#else
volatile uint64_t _BSP_PD[512]   __aligned(PAGE_SIZE);
volatile uint64_t _BSP_PDPT[512] __aligned(PAGE_SIZE);
volatile uint64_t _BSP_PML4[512] __aligned(PAGE_SIZE);
#endif

#define P   _BV(0)
#define RW  _BV(1)
#define PCD _BV(4)

extern char kernel_end;
char scratch[1024 * 1024] __aligned(PAGE_SIZE); /* 1 MiB scratch area */

static inline void enable_paging(uintptr_t page_directory)
{
    write_cr3(page_directory);
#if ARCH_BITS==32
    uint32_t cr0 = read_cr0();
    write_cr0(cr0 | CR0_PG);
#endif
}

static void switch_to_higher_half(void)
{
    uint32_t i;
    uintptr_t entries;

    /* zero out paging structure */
    memset((void *) _BSP_PD, 0, sizeof(_BSP_PD));

#if ARCH_BITS==64
    memset((void *) _BSP_PDPT, 0, sizeof(_BSP_PDPT));
    memset((void *) _BSP_PML4, 0, sizeof(_BSP_PML4));
#endif

    /* entries count required to map the kernel */
    entries = ((uintptr_t)(&kernel_end) + KERNEL_HEAP_SIZE + TABLE_MASK) / TABLE_SIZE;

#if ARCH_BITS==32
    uint32_t *_BSP_PT = (uint32_t *) scratch;

    /* identity map pages */
    for (i = 0; i < entries * 1024; ++i)
        _BSP_PT[i] = (i * PAGE_SIZE) | P | RW;

    /* map the lower-half */
    for (i = 0; i < entries; ++i)
        _BSP_PD[i] = ((uint32_t) _BSP_PT + i * PAGE_SIZE) | P | RW;

    /* map the upper-half */
    for (i = 0; i < entries; ++i)
        _BSP_PD[768 + i] = ((uint32_t) _BSP_PT + i * PAGE_SIZE) | P | RW;

    /* Enable paging using Bootstrap Processor Page Directory */
    extern void enable_paging(uint32_t);
    enable_paging((uint32_t) _BSP_PD);
#else
    uint64_t *_BSP_PT = (uint64_t *) scratch;

    /* identity map pages */
    for (i = 0; i < entries * 512; ++i)
        _BSP_PT[i] = (i * PAGE_SIZE) | P | RW;

    /* map the lower-half */
    for (i = 0; i < entries; ++i)
        _BSP_PD[i] = ((uint64_t) _BSP_PT + i * PAGE_SIZE) | P | RW;

    _BSP_PDPT[0] = (uint64_t) _BSP_PD   | P | RW;
    _BSP_PML4[0] = (uint64_t) _BSP_PDPT | P | RW;

    /* map the upper-half */
    _BSP_PML4[256] = (uint64_t) _BSP_PDPT | P | RW;

    /* Enable paging using Bootstrap Processor PML4 */
    enable_paging((uint64_t) _BSP_PML4);
#endif
}

void x86_bootstrap()
{
    /* We assume that GrUB loaded a valid GDT */
    /* Then we map the kernel to the higher half */
    switch_to_higher_half();

    /* Now we make SP in the higher half */
#if ARCH_BITS==32
    //asm volatile("add %0, %%esp"::"g"(VMA((uintptr_t) 0)):"esp");
    extern void early_init_fix_stack(uintptr_t);
    early_init_fix_stack(VMA((uintptr_t) 0));
#else
    asm volatile("add %0, %%rsp"::"r"(VMA((uintptr_t) 0)):"rsp");
#endif

    /* Ready to get out of here */
    extern void x86_cpu_init(void);
    x86_cpu_init();

    /* Why would we ever get back here? however we should be precautious */
    for (;;)
        asm volatile ("hlt;");
}
