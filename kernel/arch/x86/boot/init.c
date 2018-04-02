/**********************************************************************
 *                  Early initalization of the kernel
 *                   (Switch to higher half kernel)
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <boot/multiboot.h>
#include <boot/boot.h>

/* Minimalistic Paging structure for BSP */
volatile uint32_t _BSP_PD[1024] __aligned(PAGE_SIZE);

#define P   _BV(0)
#define RW  _BV(1)
#define PCD _BV(4)

extern char kernel_end; /* Defined in linker script */
char *lower_kernel_heap = &kernel_end;  /* Start of kernel heap */

static inline void *init_heap_alloc(size_t size, size_t align)
{
    char *ret = (char *)((uintptr_t)(lower_kernel_heap + align - 1) & (~(align - 1)));
    lower_kernel_heap = ret + size;

    memset(ret, 0, size);   /* We always clear the allocated area */

    return ret;
}

char scratch[1024 * 1024] __aligned(PAGE_SIZE); /* 1 MiB scratch area */

static inline void enable_paging(uint32_t page_directory)
{
    write_cr3(page_directory);
    uint32_t cr0 = read_cr0();
    write_cr0(cr0 | CR0_PG);
}

static void switch_to_higher_half()
{
    uint32_t i, entries;

    /* zero out paging structure */
    memset((void *) _BSP_PD, 0, sizeof(_BSP_PD));

    /* entries count required to map the kernel */
    entries = ((uint32_t)(&kernel_end) + KERNEL_HEAP_SIZE + TABLE_MASK) / TABLE_SIZE;

    uint32_t *_BSP_PT = (uint32_t *) scratch; //init_heap_alloc(entries * PAGE_SIZE, PAGE_SIZE);

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
    enable_paging((uint32_t) _BSP_PD);
}


void early_init()
{
    /* We assume that GrUB loaded a valid GDT */
    /* Then we map the kernel to the higher half */
    switch_to_higher_half();

    /* Now we make SP in the higher half */
    asm volatile("addl %0, %%esp"::"g"(VMA((uintptr_t) 0)):"esp");

    /* Ready to get out of here */
    extern void cpu_init();
    cpu_init();

    /* Why would we ever get back here? however we should be precautious */
    for(;;)
        asm volatile ("hlt;");
}
