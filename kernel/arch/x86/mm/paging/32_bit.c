/**********************************************************************
 *                 Intel 32-Bit Paging Mode Handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
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

#include <core/system.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/arch.h>
#include <cpu/cpu.h>
#include <ds/queue.h>
#include <mm/buddy.h>
#include <mm/vm.h>
#include <arch/x86/include/proc.h> /* XXX */
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include "32_bit.h"

static volatile __table_t *bootstrap_processor_table = NULL;
static volatile __page_t last_page_table[1024] __aligned(PAGE_SIZE) = {0};

static struct {
    int refs;
} __packed tables[768] = {0};

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm("invlpg (%%eax)"::"a"(virt));
}

#define MOUNT_ADDR  ((void *) 0xFFBFF000)
#define PAGE_DIR    ((__table_t *) 0xFFFFF000)
#define PAGE_TBL(i) ((__page_t *) (0xFFC00000 + 0x1000 * i))

/* ================== Frame Helpers ================== */

static inline uintptr_t frame_mount(uintptr_t paddr)
{
    //printk("frame_mount(%p)\n", paddr);
    if (!paddr)
        return (uintptr_t) last_page_table[1023].raw & ~PAGE_MASK;

    if (paddr & PAGE_MASK)
        panic("Mount must be on page (4K) boundary");

    uintptr_t prev = (uintptr_t) last_page_table[1023].raw & ~PAGE_MASK;

    __page_t page;
    page.raw = paddr;

    page.present = 1;
    page.write = 1;

    last_page_table[1023] = page;
    tlb_invalidate_page((uintptr_t) MOUNT_ADDR);

    return prev;
} 

static inline uintptr_t frame_get()
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

static inline uintptr_t frame_get_no_clr()
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

    __table_t table = {.raw = paddr};

    table.present = 1;
    table.write   = !!(flags & (VM_KW | VM_UW));
    table.user    = !!(flags & (VM_URWX));

    PAGE_DIR[pdidx] = table;

    tlb_flush();

    return 0;
}

static inline void table_unmap(size_t pdidx)
{
    if (pdidx > 1023)
        return;

    if (PAGE_DIR[pdidx].present) {
        PAGE_DIR[pdidx].present = 0;
        table_dealloc(PAGE_DIR[pdidx].raw & ~PAGE_MASK);
    }

    tlb_flush();
}

/* ================== Page Helpers ================== */

static inline int __page_map(paddr_t paddr, size_t pdidx, size_t ptidx, int flags)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023) {
        return -1;
    }

    __page_t page = {.raw = paddr};

    page.present = 1;
    page.write   = !!(flags & (VM_KW | VM_UW));
    page.user    = !!(flags & (VM_URWX));

    /* Check if table is present */
    if (!PAGE_DIR[pdidx].present) {
        paddr_t table = table_alloc();
        table_map(table, pdidx, flags);
    }

    //__page_t *page_table = PAGE_TBL(pdidx);
    PAGE_TBL(pdidx)[ptidx] = page;

    /* Increment references to table */
    tables[pdidx].refs++;

    return 0;
}

static inline void __page_unmap(vaddr_t vaddr)
{
    if (vaddr & PAGE_MASK)
        return;

    __virtaddr_t virtaddr = {.raw = vaddr};
    size_t pdidx = virtaddr.directory;
    size_t ptidx = virtaddr.table;

    if (PAGE_DIR[pdidx].present) {
        tables[pdidx].refs--;
        PAGE_TBL(pdidx)[ptidx].raw = 0;

        if (!tables[pdidx].refs)
            table_unmap(pdidx);

        tlb_invalidate_page(vaddr);
    }
}

static inline __page_t *__page_get_mapping(uintptr_t addr)
{
    __virtaddr_t virt = (__virtaddr_t){.raw = addr};

    if (PAGE_DIR[virt.directory].present) {
        __page_t *page = &PAGE_TBL(virt.directory)[virt.table];

        if (page->present)
            return page;
    }

    return NULL;
}

paddr_t arch_page_get_mapping(vaddr_t vaddr)
{
    __page_t *page = __page_get_mapping(vaddr);

    if (page)
        return GET_PHYS_ADDR(page);

    return 0;
}

#if 0
/*
 *  Copy from physical source to physical destination
 *  _phys_dest and _phys_src must be aligned to 4K
 *  Note: n is count of bytes not pages
 */

static char page_buffer[PAGE_SIZE];
static void *copy_physical_to_physical(uintptr_t _phys_dest, uintptr_t _phys_src, size_t n)
{
    //printk("copy_physical_to_physical(%p, %p, %d)\n", _phys_dest, _phys_src, n);

    if (_phys_dest & PAGE_MASK || _phys_src & PAGE_MASK)
        panic("Copy must be on page (4K) boundaries\n");

    char *phys_dest = (char *) _phys_dest;
    char *phys_src  = (char *) _phys_src;
    void *ret = phys_dest;
    uintptr_t prev_mount = frame_mount(0);

    /* Copy full pages */
    char *p = MOUNT_ADDR;
    size_t size = n / PAGE_SIZE;
    while (size--) {
        frame_mount((uintptr_t) phys_src);
        memcpy(page_buffer, p, PAGE_SIZE);
        frame_mount((uintptr_t) phys_dest);
        memcpy(p, page_buffer, PAGE_SIZE);
        phys_src += PAGE_SIZE;
        phys_dest += PAGE_SIZE;
    }

    /* Copy what is remainig */
    size = n % PAGE_SIZE;
    frame_mount((uintptr_t) phys_src);
    memcpy(page_buffer, p, size);

    frame_mount((uintptr_t) phys_dest);
    memcpy(p, page_buffer, size);

    frame_mount(prev_mount);
    return ret;
}

static inline void page_dealloc(paddr_t paddr)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023)
        panic("Invlaid PDE or PTE");

    if (PAGE_DIR[pdidx].structure.present) {
        __page_t *page_table = PAGE_TBL(pdidx);
        __page_t page = page_table[ptidx];

        if (page.structure.present) {
            page.structure.present = 0;

            size_t page_idx = page.raw/PAGE_SIZE;

            if (pages[page_idx].refs == 1)
                frame_release(page.raw & ~PAGE_MASK);

            pages[page_idx].refs--;
        }
    }
}
#endif

static void *copy_physical_to_virtual(void *_virt_dest, void *_phys_src, size_t n)
{
    //printk("copy_physical_to_virtual(v=%p, p=%p, n=%d)\n", _virt_dest, _phys_src, n);
    char *virt_dest = (char *) _virt_dest;
    char *phys_src  = (char *) _phys_src;

    void *ret = virt_dest;

    /* Copy up to page boundary */
    size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
    size_t size = MIN(n, PAGE_SIZE - offset);
    
    if (size) {
        uintptr_t prev_mount = frame_mount((uintptr_t) phys_src);
        volatile char *p = MOUNT_ADDR;
        memcpy(virt_dest, (char *) (p + offset), size);
        
        phys_src  += size;
        virt_dest += size;

        /* Copy complete pages */
        n -= size;
        size = n / PAGE_SIZE;
        while (size--) {
            frame_mount((uintptr_t) phys_src);
            memcpy(virt_dest, (char *) p, PAGE_SIZE);
            phys_src += PAGE_SIZE;
            virt_dest += PAGE_SIZE;
        }

        /* Copy what is remainig */
        size = n % PAGE_SIZE;
        if (size) {
            frame_mount((uintptr_t) phys_src);
            memcpy(virt_dest, (char *) p, size);
        }

        frame_mount(prev_mount);
    }

    return ret;
}

static void *copy_virtual_to_physical(void *_phys_dest, void *_virt_src, size_t n)
{
    char *phys_dest = (char *) _phys_dest;
    char *virt_src  = (char *) _virt_src;
    void *ret = phys_dest;

    size_t s = n / PAGE_SIZE;
    
    uintptr_t prev_mount = frame_mount(0);

    while (s--) {
        frame_mount((uintptr_t) phys_dest);
        void *p = MOUNT_ADDR;
        memcpy(p, virt_src, PAGE_SIZE);
        phys_dest += PAGE_SIZE;
        virt_src  += PAGE_SIZE;
    }

    s = n % PAGE_SIZE;

    if (s) {
        frame_mount((uintptr_t) phys_dest);
        void *p = MOUNT_ADDR;
        memcpy(p, virt_src, s);
    }

    frame_mount((uintptr_t) prev_mount);
    return ret;
}

#if 0
static int map_phys_to_virt(uintptr_t phys, uintptr_t virt, size_t size, int flags)
{
    //printk("map_to_physical(ptr=%p, size=0x%x, flags=%x)\n", ptr, size, flags);

    uintptr_t endptr = UPPER_PAGE_BOUNDARY(virt + size);
    uintptr_t ptr    = LOWER_PAGE_BOUNDARY(virt);

    size_t nr = (endptr - ptr) / PAGE_SIZE;

    while (nr--) {
        page_map_phys_to_virt(phys, ptr, flags);
        ptr += PAGE_SIZE;
        phys += PAGE_SIZE;
    }

    return 1;
}
#endif

static paddr_t cur_pd = 0;
void arch_switch_directory(paddr_t new_dir)
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
   arch_switch_directory(fork);

   uintptr_t old_mount = frame_mount(base);
   __table_t *p = (__table_t *) MOUNT_ADDR;

   for (int i = 0; i < 768; ++i) {
       if (p[i].present) {
           paddr_t table = table_alloc();
           table_map(table, i, VM_URWX);

           uintptr_t _m = frame_mount(GET_PHYS_ADDR(&p[i]));
           __page_t *_p = MOUNT_ADDR;
           
           for (int j = 0; j < 1024; ++j) {
               if (_p[j].present) {
                   PAGE_TBL(i)[j].raw = _p[j].raw;

                   if (_p[j].write) {
                       _p[j].write = 0;
                       PAGE_TBL(i)[j].write = 0;
                   }

                   pages[GET_PHYS_ADDR(&_p[j])/PAGE_SIZE].refs++;
               }
           }

           frame_mount(_m);
       }
   }

   tlb_flush();

   frame_mount(old_mount);
   arch_switch_directory(base);
}

#if 0
pmman_t pmman = (pmman_t) {
    .map = &map_to_physical,
    .map_to = &map_phys_to_virt,
    .unmap = &unmap_from_physical,
    .unmap_full = &unmap_full_from_physical,
    .memcpypv = &copy_physical_to_virtual,
    .memcpyvp = &copy_virtual_to_physical,
    .memcpypp = &copy_physical_to_physical,
    .switch_mapping = &switch_directory,
    .copy_fork_mapping = &copy_fork_mapping,
    .handle_page_fault = &handle_page_fault,
};
#endif

void setup_32_bit_paging()
{
    //printk("[0] Kernel: PMM -> Setting up 32-Bit Paging\n");
    uintptr_t __cur_pd = read_cr3() & ~PAGE_MASK;
    bootstrap_processor_table = (__table_t *) VMA(__cur_pd);

    bootstrap_processor_table[1023].raw = LMA((uint32_t) bootstrap_processor_table);
    bootstrap_processor_table[1023].write = 1;
    bootstrap_processor_table[1023].present = 1;

    bootstrap_processor_table[1022].raw = LMA((uint32_t) last_page_table);
    bootstrap_processor_table[1022].write = 1;
    bootstrap_processor_table[1022].present = 1;

    /* Unmap lower half */
    for (int i = 0; bootstrap_processor_table[i].raw; ++i)
        bootstrap_processor_table[i].raw = 0;

    tlb_flush();
}

/*
 *  Archeticture Interface
 */

uintptr_t arch_get_frame()
{
    return frame_get();
}

uintptr_t arch_get_frame_no_clr()
{
    return frame_get_no_clr();
}

void arch_release_frame(uintptr_t p)
{
    frame_release(p);
}

int arch_page_map(paddr_t paddr, vaddr_t vaddr, int flags)
{
    if (vaddr & PAGE_MASK)
        return -EINVAL;

    __virtaddr_t virtaddr = {.raw = vaddr};
    size_t pdidx = virtaddr.directory;
    size_t ptidx = virtaddr.table;

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
    __page_t *page = __page_get_mapping(page_addr);

    if (page) { /* Page is mapped */
        uintptr_t phys = GET_PHYS_ADDR(page);
        size_t page_idx = phys/PAGE_SIZE;

        if (pages[page_idx].refs == 1) {
            page->write = 1;
            tlb_invalidate_page(vaddr);
        } else {
            pages[page_idx].refs--;
            page->present = 0;
            mm_map(0, page_addr, PAGE_SIZE, VM_URWX);   /* FIXME */ 
            copy_physical_to_virtual((void *) page_addr, (void *) phys, PAGE_SIZE);
        }

        return;
    }

    mm_page_fault(vaddr);
}
