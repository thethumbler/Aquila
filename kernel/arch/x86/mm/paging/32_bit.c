/**********************************************************************
 *                 Intel 32-Bit Paging Mode Handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016-2017 Mohamed Anwar <mohamed_anwar@opmbx.org>
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

static inline void tlb_invalidate_page(uintptr_t virt)
{
    asm("invlpg (%%eax)"::"a"(virt));
}

#define MOUNT_ADDR ((void *) 0xFFBFF000)
#define PAGE_DIR ((__table_t *) 0xFFFFF000)
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
    page.structure.present = 1;
    page.structure.write = 1;

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
    /* Release frame if it is mapped */
    if (i & 0x1)
        buddy_free(BUDDY_ZONE_NORMAL, i, PAGE_SIZE);
}

/* ================== Page Helpers ================== */

static inline void table_alloc(size_t pdidx, int flags)
{
    //printk("alloc_table(pdidx=%d, flags=0x%x\n", pdidx, flags);

    if (pdidx > 1023)
        panic("Invlaid PDE");

    __table_t table = {.raw = frame_get()};

    table.structure.present = 1;
    table.structure.write = !!(flags & (VM_KW | VM_UW));
    table.structure.user = !!(flags & (VM_URWX));

    PAGE_DIR[pdidx] = table;
    TLB_flush();
}

static inline void table_dealloc(size_t pdidx)
{
    //printk("alloc_page(pdidx=%d, ptidx=%d, flags=0x%x\n", pdidx, ptidx, flags);

    if (pdidx > 1023)
        panic("Invlaid PDE");

    if (PAGE_DIR[pdidx].structure.present) {
        PAGE_DIR[pdidx].structure.present = 0;
        frame_release(PAGE_DIR[pdidx].raw & ~PAGE_MASK);
    }

    TLB_flush();
}

/* ================== Page Helpers ================== */

static inline void page_map_phys(uintptr_t paddr, size_t pdidx, size_t ptidx, int flags)
{
    /* Sanity checking */
    if (pdidx > 1023 || ptidx > 1023)
        panic("Invlaid PDE or PTE");

    __page_t *page_table = PAGE_TBL(pdidx);

    __page_t page = {.raw = paddr};

    page.structure.present = 1;
    page.structure.write = !!(flags & (VM_KW | VM_UW));
    page.structure.user = !!(flags & (VM_URWX));

    page_table[ptidx] = page;
}

static inline void page_alloc(size_t pdidx, size_t ptidx, int flags)
{
    /* Get new frame */
    uintptr_t paddr = frame_get_no_clr();
    /* Map page to physical address */
    page_map_phys(paddr, pdidx, ptidx, flags);
    /* Increment references count to physical page */
    pages[paddr/PAGE_SIZE].refs++;
}

static inline void page_dealloc(size_t pdidx, size_t ptidx)
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

static inline void page_map_phys_to_virt(uintptr_t phys, uintptr_t virt, int flags)
{
    if (virt & PAGE_MASK)
        panic("Invalid Virtual address");

    __virtaddr_t virtaddr = {.raw = virt};
    size_t pdidx = virtaddr.structure.directory;
    size_t ptidx = virtaddr.structure.table;

    if (!PAGE_DIR[pdidx].structure.present) {
        table_alloc(pdidx, flags);
    }

    page_map_phys(phys, pdidx, ptidx, flags);
    tlb_invalidate_page(virt);
}

static inline void page_map(uintptr_t virt, int flags)
{
    if (virt & PAGE_MASK)
        panic("Invalid Virtual address");

    __virtaddr_t virtaddr = {.raw = virt};
    size_t pdidx = virtaddr.structure.directory;
    size_t ptidx = virtaddr.structure.table;

    if (!PAGE_DIR[pdidx].structure.present) {
        table_alloc(pdidx, flags);
    }

    __page_t *page_table = PAGE_TBL(pdidx);

    if (!page_table[ptidx].structure.present) {
        page_alloc(pdidx, ptidx, flags);
    }

    tlb_invalidate_page(virt);
}

static inline void page_unmap(uintptr_t virt)
{
    //printk("unmap_page(virt=%p)\n", virt);

    if (virt & PAGE_MASK)
        panic("Invalid Virtual address");

    __virtaddr_t virtaddr = {.raw = virt};
    size_t pdidx = virtaddr.structure.directory;
    size_t ptidx = virtaddr.structure.table;

    if (PAGE_DIR[pdidx].structure.present) {
        __page_t *page_table = PAGE_TBL(pdidx);

        if (page_table[ptidx].structure.present) {
            page_dealloc(pdidx, ptidx);
        }

        tlb_invalidate_page(virt);
    }
}

static inline __page_t *page_get_mapping(uintptr_t addr)
{
    __virtaddr_t virt = (__virtaddr_t){.raw = addr};

    if (PAGE_DIR[virt.structure.directory].structure.present) {
        __page_t *page = &PAGE_TBL(virt.structure.directory)[virt.structure.table];

        if (page->structure.present)
            return page;
    }

    return NULL;
}



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

static int map_to_physical(uintptr_t ptr, size_t size, int flags)
{
    //printk("map_to_physical(ptr=%p, size=0x%x, flags=%x)\n", ptr, size, flags);

    uintptr_t endptr = UPPER_PAGE_BOUNDARY(ptr + size);
    ptr = LOWER_PAGE_BOUNDARY(ptr);

    size_t nr = (endptr - ptr) / PAGE_SIZE;

    while (nr--) {
        page_map(ptr, flags);
        ptr += PAGE_SIZE;
    }

    return 1;
}

static void unmap_from_physical(uintptr_t ptr, size_t size)
{
    //printk("unmap_from_physical(ptr=%p, size=0x%x)\n", ptr, size);
    if (size < PAGE_SIZE) return;

    uintptr_t start = UPPER_PAGE_BOUNDARY(ptr);
    uintptr_t end   = LOWER_PAGE_BOUNDARY(ptr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        page_unmap(start);
        start += PAGE_SIZE;
    }

    /* Dealloc Tables */
    start = UPPER_TABLE_BOUNDARY(ptr);
    end   = LOWER_TABLE_BOUNDARY(ptr + size);

    if (end > start) {
        nr = (end - start)/TABLE_SIZE;

        start /= TABLE_SIZE;
        while (nr--) {
            table_dealloc(start);
            start += 1;
        }
    }
}

static void unmap_full_from_physical(uintptr_t ptr, size_t size)
{
    //printk("unmap_full_from_physical(ptr=%p, size=0x%x)\n", ptr, size);

    /* Unmap pages */
    uintptr_t start = LOWER_PAGE_BOUNDARY(ptr);
    uintptr_t end   = UPPER_PAGE_BOUNDARY(ptr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        page_unmap(start);
        start += PAGE_SIZE;
    }

    /* Dealloc Tables */
    start = LOWER_TABLE_BOUNDARY(ptr);
    end   = UPPER_TABLE_BOUNDARY(ptr + size);

    nr = (end - start)/TABLE_SIZE;

    start /= TABLE_SIZE;
    while (nr--) {
        table_dealloc(start);
        start += 1;
    }
}

static uintptr_t cur_pd = 0;
static void switch_directory(uintptr_t new_dir)
{
    //printk("switch_directory(%p)\n", new_dir);
    if (cur_pd == new_dir) return;

    if (cur_pd) { /* Store current directory mapping in old_dir */
        //printk("Storing old directory in %p\n", cur_pd);
        copy_virtual_to_physical((void *) cur_pd, (void *) bootstrap_processor_table, 768 * 4);
    }

    copy_physical_to_virtual((void *) bootstrap_processor_table, (void *) new_dir, 768 * 4);
    cur_pd = new_dir;
    TLB_flush();
}

/* Lazy fork */
static void copy_fork_mapping(uintptr_t base, uintptr_t fork)
{
   //printk("copy_fork_mapping(%p, %p)\n", base, fork);
   switch_directory(fork);

   uintptr_t old_mount = frame_mount(base);
   __table_t *p = (__table_t *) MOUNT_ADDR;

   for (int i = 0; i < 768; ++i) {
       if (p[i].structure.present) {
           table_alloc(i, VM_URWX);    // FIXME
           uintptr_t _m = frame_mount(GET_PHYS_ADDR(&p[i]));
           __page_t *_p = MOUNT_ADDR;
           
           for (int j = 0; j < 1024; ++j) {
               if (_p[j].structure.present) {
                   __page_t *page_table = (__page_t *) (0xFFC00000 + 0x1000 * i);
                   page_table[j].raw = _p[j].raw;

                   if (_p[j].structure.write) {
                       _p[j].structure.write = 0;
                       page_table[j].structure.write = 0;
                   }

                   pages[GET_PHYS_ADDR(&_p[j])/PAGE_SIZE].refs++;
               }
           }

           frame_mount(_m);
       }
   }

   TLB_flush();

   frame_mount(old_mount);
   switch_directory(base);
}

void handle_page_fault(uintptr_t addr)
{
    //printk("handle_page_fault(addr=%p)\n", addr);

    uintptr_t page_addr = addr & ~PAGE_MASK;
    __page_t *page = page_get_mapping(page_addr);

    if (page) { /* Page is mapped */
        uintptr_t phys = GET_PHYS_ADDR(page);
        size_t page_idx = phys/PAGE_SIZE;

        if ((addr >= USER_STACK_BASE && addr < USER_STACK_BASE + USER_STACK_SIZE)   /* Stack? */
             || (addr < cur_thread->owner->heap_start)   /* Data section? FIXME */
             || (addr < cur_thread->owner->heap)) {  /* Allocated Heap */
            if (pages[page_idx].refs == 1) {
                page->structure.write = 1;
                tlb_invalidate_page(addr);
            } else {
                pages[page_idx].refs--;
                page->structure.present = 0;
                page_map(page_addr, VM_URWX);   /* FIXME */ 
                copy_physical_to_virtual((void *) page_addr, (void *) phys, PAGE_SIZE);
            }

            return;
        }
    } else {
#if 1
        queue_t *q_vmr = &cur_thread->owner->vmr;
        int vmr_flag = 0;
        uintptr_t page_end = page_addr + PAGE_SIZE;

        forlinked (node, q_vmr->head, node->next) {
            struct vmr *vmr = node->value;
            uintptr_t vmr_end = vmr->base + vmr->size;

            /* exclude non overlapping VMRs */
            if (page_end <= vmr->base || page_addr >= vmr_end)
                continue;

            /* page overlaps vmr in at least one byte */
            vmr_flag = 1;
            page_map(page_addr, VM_URWX);  /* FIXME */

            uintptr_t addr_start = page_addr;
            if (vmr->base > page_addr)
                addr_start = vmr->base;

            uintptr_t addr_end = page_end;
            if (vmr_end < page_end)
                addr_end = vmr_end;

            size_t size = addr_end - addr_start;
            size_t file_off = addr_start - vmr->base;

            /* File backed */
            if (vmr->flags & VM_FILE)
                vfs_read(vmr->inode, vmr->off + file_off, size, (void *) addr_start);

            /* Zero fill */
            if (vmr->flags & VM_ZERO)
                memset((void *) addr_start, 0, size);
        }

        if (vmr_flag)
            return;
#endif

        if (addr < cur_thread->owner->heap && addr >= cur_thread->owner->heap_start) {
            page_map(page_addr, VM_URWX);  /* FIXME */
            memset((void *) page_addr, 0, PAGE_SIZE);
            return;
        }

        // Send signal
        signal_proc_send(cur_thread->owner, SIGSEGV);
        return;

#if 0
        printk("[%d:%d] %s\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
        printk("-- addr %p\n", addr);
        printk("-- heap %p\n", cur_thread->owner->heap);
        printk("-- arch %p\n", cur_thread->arch);
        x86_thread_t *arch = cur_thread->arch;
        printk("-- arch->regs %p\n", arch->regs);
        x86_dump_registers(arch->regs);
        panic("Not implemented\n");
#endif
    }

    x86_thread_t *arch = cur_thread->arch;
    printk("CR2 %p\n", read_cr2());
    x86_dump_registers(arch->regs);
    panic("This is fucked up :)\n");
}

void setup_32_bit_paging()
{
    //printk("[0] Kernel: PMM -> Setting up 32-Bit Paging\n");
    uintptr_t __cur_pd = read_cr3() & ~PAGE_MASK;
    bootstrap_processor_table = (__table_t *) VMA(__cur_pd);

    bootstrap_processor_table[1023].raw = LMA((uint32_t) bootstrap_processor_table);
    bootstrap_processor_table[1023].structure.write = 1;
    bootstrap_processor_table[1023].structure.present = 1;

    bootstrap_processor_table[1022].raw = LMA((uint32_t) last_page_table);
    bootstrap_processor_table[1022].structure.write = 1;
    bootstrap_processor_table[1022].structure.present = 1;

    /* Unmap lower half */
    for (int i = 0; bootstrap_processor_table[i].raw; ++i)
        bootstrap_processor_table[i].raw = 0;

    TLB_flush();
}

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
