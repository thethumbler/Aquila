/**********************************************************************
 *                 Intel 32-Bit Paging Mode Handler
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 *
 *  References:
 *      [1] - Intel(R) 64 and IA-32 Architectures Software Developers Manual
 *          - Volume 3, System Programming Guide
 *          - Chapter 4, Paging
 *
 */

#include <core/arch.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/system.h>
#include <cpu/cpu.h>
#include <ds/queue.h>
#include <mm/buddy.h>
#include <mm/vm.h>

#include "32_bit.h"

static volatile uint32_t *bootstrap_processor_table = NULL;
static volatile uint32_t last_page_table[1024] __aligned(PAGE_SIZE) = {0};

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm volatile("invlpg (%%eax)"::"a"(virt));
}

#define MOUNT_ADDR  ((void *) 0xFFBFF000)
#define PAGE_DIR    ((uint32_t *) 0xFFFFF000)
#define PAGE_TBL(i) ((uint32_t *) (0xFFC00000 + 0x1000 * (i)))

/* ================== Frame Helpers ================== */

static inline uintptr_t frame_mount(uintptr_t paddr)
{
    if (!paddr)
        return (uintptr_t) last_page_table[1023] & ~PAGE_MASK;

    if (paddr & PAGE_MASK)
        panic("Mount must be on page (4K) boundary");

    uintptr_t prev = (uintptr_t) last_page_table[1023] & ~PAGE_MASK;

    uint32_t page;

    page = paddr | PG_PRESENT | PG_WRITE;

    last_page_table[1023] = page;
    tlb_invalidate_page((uintptr_t) MOUNT_ADDR);

    return prev;
} 

static inline uintptr_t frame_get(void)
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    uintptr_t old = frame_mount(frame);
    memset(MOUNT_ADDR, 0, PAGE_SIZE);
    frame_mount(old);

    return frame;
}

static inline uintptr_t frame_get_no_clr(void)
{
    uintptr_t frame = buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);

    if (!frame) {
        panic("Could not allocate frame");
    }

    return frame;
}

static void frame_release(uintptr_t i)
{
    buddy_free(BUDDY_ZONE_NORMAL, i, PAGE_SIZE);
}

/* ================== Table Helpers ================== */

static inline paddr_t table_alloc(void)
{
    return frame_get();
}

static inline void table_dealloc(paddr_t paddr)
{
    frame_release(paddr);
}

static inline int table_map(paddr_t paddr, size_t pdidx, int flags)
{
    if (pdidx > 1023)
        return -EINVAL;

    uint32_t table;
    table  = paddr | PG_PRESENT;
    table |= flags & (VM_KW | VM_UW)? PG_WRITE : 0;
    table |= flags & (VM_URWX)? PG_USER : 0;

    PAGE_DIR[pdidx] = table;

    tlb_flush();

    return 0;
}

static inline void table_unmap(size_t pdidx)
{
    if (pdidx > 1023)
        return;

    if (PAGE_DIR[pdidx] & PG_PRESENT) {
        PAGE_DIR[pdidx] &= ~PG_PRESENT;
        table_dealloc(PAGE_DIR[pdidx] & ~PAGE_MASK);
    }

    tlb_flush();
}

/* ================== Page Helpers ================== */

static inline int __page_map(paddr_t paddr, size_t pdidx, size_t ptidx, int flags)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023) {
        return -EINVAL;
    }

    uint32_t page;

    page  = paddr | PG_PRESENT;
    page |= flags & (VM_KW | VM_UW)? PG_WRITE : 0;
    page |= flags & (VM_URWX)? PG_USER : 0;

    /* Check if table is present */
    if (!(PAGE_DIR[pdidx] & PG_PRESENT)) {
        paddr_t table = table_alloc();
        table_map(table, pdidx, flags);
    }

    PAGE_TBL(pdidx)[ptidx] = page;

    /* Increment references to table */
    paddr_t table = GET_PHYS_ADDR(PAGE_DIR[pdidx]);
    pages[table/PAGE_SIZE].refs++;

    return 0;
}

static inline void __page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return;

    size_t pdidx = VDIR(vaddr);
    size_t ptidx = VTBL(vaddr);

    if (PAGE_DIR[pdidx] & PG_PRESENT) {

        PAGE_TBL(pdidx)[ptidx] = 0;

        /* Decrement references to table */
        paddr_t table = GET_PHYS_ADDR(PAGE_DIR[pdidx]);
        pages[table/PAGE_SIZE].refs--;

        if (!pages[table/PAGE_SIZE].refs)
            table_unmap(pdidx);

        tlb_invalidate_page(vaddr);
    }
}

static inline uint32_t __page_get_mapping(vaddr_t vaddr)
{
    if (PAGE_DIR[VDIR(vaddr)] & PG_PRESENT) {
        uint32_t page = PAGE_TBL(VDIR(vaddr))[VTBL(vaddr)];

        if (page & PG_PRESENT)
            return page;
    }

    return 0;
}

paddr_t arch_page_get_mapping(vaddr_t vaddr)
{
    //printk("arch_page_get_mapping(vaddr=%p)\n", vaddr);
    uint32_t page = __page_get_mapping(vaddr);

    if (page)
        return GET_PHYS_ADDR(page);

    return 0;
}

#include "page_utils.h"

int __i = 0;
static paddr_t cur_pd = 0;
void arch_switch_mapping(paddr_t new_dir)
{
    //printk("switch_directory(%p)\n", new_dir);
    if (cur_pd == new_dir) return;

    if (cur_pd) { /* Store current directory mapping in old_dir */
        copy_virtual_to_physical((void *) cur_pd, (void *) bootstrap_processor_table, 768 * 4);
    }

    copy_physical_to_virtual((void *) bootstrap_processor_table, (void *) new_dir, 768 * 4);
    cur_pd = new_dir;
    tlb_flush();
}

void arch_mm_fork(paddr_t base, paddr_t fork)
{
    //printk("arch_mm_fork(%p, %p)\n", base, fork);

    arch_switch_mapping(fork);

    uintptr_t old_mount = frame_mount(base);
    uint32_t *tbl = (uint32_t *) MOUNT_ADDR;

    for (int i = 0; i < 768; ++i) {
        if (tbl[i] & PG_PRESENT) {
            /* Allocate new table for mapping */
            paddr_t table = table_alloc();
            table_map(table, i, VM_URWX);

            uintptr_t _mnt = frame_mount(GET_PHYS_ADDR(tbl[i]));
            uint32_t  *pg  = MOUNT_ADDR;

            
            for (int j = 0; j < 1024; ++j) {
                if (pg[j] & PG_PRESENT) {
                    if (pg[j] & PG_WRITE)
                        pg[j] &= ~PG_WRITE;

                    PAGE_TBL(i)[j] = pg[j];
                    pages[table/PAGE_SIZE].refs++;

                    pages[GET_PHYS_ADDR(pg[j])/PAGE_SIZE].refs++;
                }
            }

            frame_mount(_mnt);
        }
    }

    tlb_flush();

    frame_mount(old_mount);
    arch_switch_mapping(base);
}

void setup_32_bit_paging()
{
    printk("x86: Setting up 32-Bit paging\n");
    uintptr_t __cur_pd = read_cr3() & ~PAGE_MASK;
    bootstrap_processor_table = (uint32_t *) VMA(__cur_pd);

    bootstrap_processor_table[1023] = LMA((uint32_t) bootstrap_processor_table) | PG_WRITE | PG_PRESENT;

    bootstrap_processor_table[1022] = LMA((uint32_t) last_page_table) | PG_PRESENT | PG_WRITE;

    /* Unmap lower half */
    for (int i = 0; bootstrap_processor_table[i]; ++i)
        bootstrap_processor_table[i] = 0;

    tlb_flush();
}

/*
 *  Archeticture Interface
 */

paddr_t arch_get_frame()
{
    return frame_get();
}

uintptr_t arch_get_frame_no_clr()
{
    return frame_get_no_clr();
}

void arch_release_frame(paddr_t p)
{
    frame_release(p);
}

int arch_page_map(paddr_t paddr, vaddr_t vaddr, int flags)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    size_t pdidx = VDIR(vaddr);
    size_t ptidx = VTBL(vaddr);

    __page_map(paddr, pdidx, ptidx, flags);

    tlb_invalidate_page(vaddr);

    return 0;
}

int arch_page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    __page_unmap(vaddr);
    return 0;
}

void arch_mm_page_fault(vaddr_t vaddr)
{
    vaddr_t page_addr = vaddr & ~PAGE_MASK;
    uint32_t page = __page_get_mapping(page_addr);

    if (page) { /* Page is mapped */
        paddr_t paddr = GET_PHYS_ADDR(page);
        size_t page_idx = paddr/PAGE_SIZE;

        if (pages[page_idx].refs == 1) {
            PAGE_TBL(VDIR(page_addr))[VTBL(page_addr)] |= PG_WRITE;
            tlb_invalidate_page(vaddr);
        } else {
            pages[page_idx].refs--;
            PAGE_TBL(VDIR(page_addr))[VTBL(page_addr)] &= ~PG_PRESENT;
            mm_map(0, page_addr, PAGE_SIZE, VM_URWX);   /* FIXME */ 
            copy_physical_to_virtual((void *) page_addr, (void *) paddr, PAGE_SIZE);
        }

        return;
    }

    mm_page_fault(vaddr);
}
