/**********************************************************************
 *                  Physical Memory Manager
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <cpu/cpu.h>
#include <boot/multiboot.h>
#include <boot/boot.h>
#include <mm/mm.h>

volatile uint32_t *BSP_PD = NULL;
static volatile uint32_t BSP_LPT[PAGE_SIZE/4] __aligned(PAGE_SIZE) = {0};

#define P   (1 << 0)
#define RW  (1 << 1)

#define MOUNT_ADDR ((void *) ~PAGE_MASK)
static void *mount(uintptr_t paddr)
{
    void *prev = (void *) BSP_LPT[1023];
    BSP_LPT[1023] = (paddr & (~PAGE_MASK)) | P | RW;
    asm("invlpg (%%eax)"::"a"(~PAGE_MASK));

    return prev;
} 

static uintptr_t get_frame()
{
    uintptr_t page = buddy_alloc(PAGE_SIZE);

    if (!page) {
        //buddy_dump();
        panic("Could not allocate frame");
    }

    mount(page);
    memset(MOUNT_ADDR, 0, PAGE_SIZE);

    return page;
}

void release_frame(uintptr_t i)
{
    /* Release frame if it's mapped */
    if (i & 0x1)
        buddy_free(i, PAGE_SIZE);
}

uintptr_t get_frame_no_clr()
{
    return buddy_alloc(PAGE_SIZE);
}

static int map_to_physical(uintptr_t ptr, size_t size, int flags)
{
    //printk("map_to_physical(ptr=%p, size=0x%x, flags=%x)\n", ptr, size, flags);
    size += ptr & PAGE_MASK;
    ptr &= ~PAGE_MASK;
    size_t nr = (size + PAGE_MASK) / PAGE_SIZE;

    if (!nr) return 0;

    flags =  0x1 | ((flags & (KW | UW)) ? 0x2 : 0x0)
        | ((flags & URWX) ? 0x4 : 0x0);

    uint32_t *cur_pd_phys = (uint32_t *) read_cr3();
    //printk("cur_pd_phys = %x\n", cur_pd_phys);
    uint32_t cur_pd[PAGE_SIZE/4];
    pmman.memcpypv(cur_pd, cur_pd_phys, PAGE_SIZE);

    /* Number of tables needed for mapping */
    uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

    /* Number of pages needed for mapping */
    uint32_t pages  = (ptr + size + PAGE_MASK)/PAGE_SIZE - ptr/PAGE_SIZE;

    /* Index of the first table in the Page Directory */
    uint32_t tindex  = ptr/TABLE_SIZE;

    /* Index of the first page in the first table */
    uint32_t pindex = (ptr & TABLE_MASK)/PAGE_SIZE;

    uint32_t i;
    for (i = 0; i < tables; ++i) {
        /* If table is not allocated already, get one
           note that the frame we just got is already mounted */
        if (!(cur_pd[tindex + i] & 1))
            cur_pd[tindex + i] = get_frame() | flags;
        else /* Otherwise, just mount the already allocated table */
            mount(cur_pd[tindex + i] & ~PAGE_MASK);

        /* Mounted pages are mapped to the last page of 4GB system */
        uint32_t *page_table = (uint32_t *) MOUNT_ADDR; /* Last page */

        while (pages--) {   /* Repeat as long as we still have pages to allocate */
            /* Allocate page if it is not already allocated
               Note that get_frame_no_clr() does not mount the page */
            if (!(page_table[pindex] & 1))
                page_table[pindex] = get_frame_no_clr() | flags;

            ++pindex;
            if (pindex == 1024) break; /* Get out once the table is filled */
        }

        pindex = 0; /* Now we are pointing to a new table so we clear pindex */
    }

    pmman.memcpyvp(cur_pd_phys, cur_pd, PAGE_SIZE);

    TLB_flush();

    return 1;
}

static void unmap_from_physical(uintptr_t ptr, size_t size)
{
    if (size < PAGE_SIZE) return;

    size -= PAGE_SIZE - (ptr & PAGE_MASK);
    ptr |= PAGE_MASK;
    size_t nr = size/PAGE_SIZE;

    if (!nr) return;

    uint32_t *cur_pd_phys = (uint32_t *) read_cr3();
    uint32_t cur_pd[PAGE_SIZE/4];
    pmman.memcpypv(cur_pd, cur_pd_phys, PAGE_SIZE);

    /* Number of tables that are mapped, we unmap only on table boundary */
    uint32_t tables = (ptr + size + TABLE_MASK)/TABLE_SIZE - ptr/TABLE_SIZE;

    /* Number of pages to be unmaped */
    uint32_t pages  = (ptr + size)/PAGE_SIZE - (ptr + PAGE_MASK)/PAGE_SIZE;

    /* Index of the first table in the Page Directory */
    uint32_t tindex = ptr/TABLE_SIZE;

    /* Index of the first page in the first table */
    uint32_t pindex = ((ptr & TABLE_MASK) + PAGE_MASK)/PAGE_SIZE;

    void *prev_mount = mount(cur_pd[tindex]);

    /* We first check if our pindex, is the first page of the first table */
    if (pindex) {   /* pindex does not point to the first page of the first table */

        /* We proceed if the table is already mapped, otherwise, skip it */
        if (cur_pd[tindex] & 1) {
            /* First, we mount the table */
            mount(cur_pd[tindex]);
            uint32_t *PT = MOUNT_ADDR;

            /* We unmap pages only, the table is not unmapped */
            while (pages && (pindex < 1024)) {
                release_frame(PT[pindex]);
                PT[pindex] = 0;
                ++pindex;
                --pages;
            }
        }

        /*We point to the next table, decrement tables count and reset pindex */
        --tables;
        ++tindex;
        pindex = 0;
    }

    /* Now we iterate over all remaining tables */
    while (tables--) {
        /* unmapping already mapped tables only, othewise skip */
        if (cur_pd[tindex] & 1) {
            /* Mount the table so we can modify it */
            mount(cur_pd[tindex] & ~PAGE_MASK);
            uint32_t *PT = MOUNT_ADDR;

            /* iterate over pages, stop if we reach the final page or the number
               of pages left to unmap is zero */
            while ((pindex < 1024) && pages) {
                release_frame(PT[pindex]);
                PT[pindex] = 0;
                ++pindex;
                --pages;
            }

            if (pindex == 1024) { /* unamp table only if we reach it's last page */
                release_frame(cur_pd[tindex]);
                cur_pd[tindex] = 0;
            }

            /* reset pindex */
            pindex = 0;
        }

        ++tindex;
    }

    pmman.memcpyvp(cur_pd_phys, cur_pd, PAGE_SIZE);

    mount((uintptr_t) prev_mount);
}

static void *memcpypv(void *_virt_dest, void *_phys_src, size_t n)
{
    char *virt_dest = (char *) _virt_dest;
    char *phys_src  = (char *) _phys_src;

    void *ret = virt_dest;

    /* Copy up to page boundary */
    size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
    size_t size = MIN(n, PAGE_SIZE - offset);
    
    void *prev_mount = NULL;

    if (size) {
        prev_mount = mount((uintptr_t) phys_src);
        char *p = MOUNT_ADDR;
        memcpy(virt_dest, p + offset, size);
        
        phys_src  += size;
        virt_dest += size;

        /* Copy complete pages */
        n -= size;
        size = n / PAGE_SIZE;
        while (size--) {
            mount((uintptr_t) phys_src);
            memcpy(virt_dest, p, PAGE_SIZE);
            phys_src += PAGE_SIZE;
            virt_dest += PAGE_SIZE;
        }

        /* Copy what is remainig */
        size = n % PAGE_SIZE;
        if (size) {
            mount((uintptr_t) phys_src);
            memcpy(virt_dest, p, size);
        }
    }

    mount((uintptr_t) prev_mount);

    return ret;
}

static void *memcpyvp(void *_phys_dest, void *_virt_src, size_t n)
{
    char *phys_dest = (char *) _phys_dest;
    char *virt_src  = (char *) _virt_src;
    void *ret = phys_dest;

    size_t s = n / PAGE_SIZE;
    
    void *prev_mount = mount((uintptr_t) phys_dest);

    while (s--) {
        mount((uintptr_t) phys_dest);
        void *p = MOUNT_ADDR;
        memcpy(p, virt_src, PAGE_SIZE);
        phys_dest += PAGE_SIZE;
        virt_src  += PAGE_SIZE;
    }

    s = n % PAGE_SIZE;

    mount((uintptr_t) phys_dest);
    void *p = MOUNT_ADDR;
    memcpy(p, virt_src, s);
    mount((uintptr_t) prev_mount);
    return ret;
}

static char memcpy_pp_buf[PAGE_SIZE];
static void *memcpypp(void *_phys_dest, void *_phys_src, size_t n)
{
    /* XXX: This function is catastrophic */
    char *phys_dest = (char *) _phys_dest;
    char *phys_src  = (char *) _phys_src;
    void *ret = phys_dest;

    /* Copy up to page boundary */
    size_t offset = (uintptr_t) phys_src % PAGE_SIZE;
    size_t size = MIN(n, PAGE_SIZE - offset);

    mount((uintptr_t) phys_src);
    char *p = MOUNT_ADDR;
    memcpy(memcpy_pp_buf + offset, p + offset, size);

    mount((uintptr_t) phys_dest);
    memcpy(p + offset, memcpy_pp_buf + offset, size);
    
    phys_src  += size;
    phys_dest += size;
    n -= size;

    /* Copy complete pages */
    size = n / PAGE_SIZE;
    while (size--) {
        mount((uintptr_t) phys_src);
        memcpy(memcpy_pp_buf, p, PAGE_SIZE);
        mount((uintptr_t) phys_dest);
        memcpy(p, memcpy_pp_buf, PAGE_SIZE);
        phys_src += PAGE_SIZE;
        phys_dest += PAGE_SIZE;
    }

    /* Copy what is remainig */
    size = n % PAGE_SIZE;
    mount((uintptr_t) phys_src);
    memcpy(memcpy_pp_buf, p, size);

    mount((uintptr_t) phys_dest);
    memcpy(p, memcpy_pp_buf, size);

    return ret;
}


/*
 *  Archeticture Interface
 */

uintptr_t arch_get_frame()
{
    return get_frame();
}

uintptr_t arch_get_frame_no_clr()
{
    return get_frame_no_clr();
}

void arch_release_frame(uintptr_t p)
{
    release_frame(p);
}


/*
 *  Physical Memory Manger Setup
 */

pmman_t pmman = (pmman_t) {
    .map = &map_to_physical,
    .unmap = &unmap_from_physical,
    .memcpypv = &memcpypv,
    .memcpyvp = &memcpyvp,
    .memcpypp = &memcpypp,
};


void arch_pmm_setup()
{
    /* Fix kernel heap pointer */
    extern char *lower_kernel_heap;
    extern char *kernel_heap;
    kernel_heap = VMA(lower_kernel_heap);

    struct cpu_features features;
    get_cpu_features(&features);

#if !defined(X86_PAE) || !X86_PAE
    if (features.pse) {
        //write_cr4(read_cr4() | CR4_PSE);
        printk("[0] Kernel: PMM -> Found PSE support\n");
    }
#else   /* PAE */
    if (features.pae) {
        printk("[0] Kernel: PMM -> Found PAE support\n");
    } else if(features.pse) {
        printk("[0] Kernel: PMM -> Found PSE support\n");
    }
#endif

    BSP_PD = (uint32_t *) VMA(read_cr3());
    BSP_PD[1023] = LMA((uint32_t) BSP_LPT) | P | RW;
    TLB_flush();

    for (;;);
}
