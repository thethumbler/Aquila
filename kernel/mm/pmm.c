/**********************************************************************
 *                  Physical Memory Manager
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/arch.h>
#include <boot/boot.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/buddy.h>
#include <sys/sched.h>

static inline paddr_t __page_alloc(void)
{
    return buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);
}

static inline void __page_release(paddr_t paddr)
{
    buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
}

struct page pages[768*1024] = {0};

static inline paddr_t mm_page_alloc(void)
{
    /* Get new frame */
    paddr_t paddr = __page_alloc();
    
    /* Increment references count to physical page */
    pages[paddr/PAGE_SIZE].refs++;

    return paddr;
}

static inline void mm_page_dealloc(paddr_t paddr)
{
    /* Decrement references count to physical page */
    pages[paddr/PAGE_SIZE].refs--;

    if (!pages[paddr/PAGE_SIZE].refs)
        __page_release(paddr);
}

int mm_page_map(paddr_t paddr, vaddr_t vaddr, int flags)
{
    return arch_page_map(paddr, vaddr, flags);
}

int mm_page_unmap(vaddr_t vaddr)
{
    return arch_page_unmap(vaddr);
}

int mm_map(paddr_t paddr, vaddr_t vaddr, size_t size, int flags)
{
    //printk("mm_map(%p, %p, %x, %x)\n", paddr, vaddr, size, flags);
    int alloc = !paddr;

    vaddr_t endaddr = UPPER_PAGE_BOUNDARY(vaddr + size);
    vaddr = LOWER_PAGE_BOUNDARY(vaddr);
    paddr = LOWER_PAGE_BOUNDARY(paddr);

    size_t nr = (endaddr - vaddr) / PAGE_SIZE;

    while (nr--) {
        paddr_t phys = arch_page_get_mapping(vaddr);
        //printk("phys %p\n", phys);

        if (!phys) {
            if (alloc)
                paddr = mm_page_alloc();

            //printk("paddr %p\n", paddr);
            mm_page_map(paddr, vaddr, flags);
        }

        vaddr += PAGE_SIZE;
        paddr += PAGE_SIZE;
    }

    return 0;
}

void mm_unmap(vaddr_t vaddr, size_t size)
{
    if (size < PAGE_SIZE) return;

    vaddr_t start = UPPER_PAGE_BOUNDARY(vaddr);
    vaddr_t end   = LOWER_PAGE_BOUNDARY(vaddr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(start);
        start += PAGE_SIZE;
    }
}

void mm_unmap_full(vaddr_t vaddr, size_t size)
{
    vaddr_t start = LOWER_PAGE_BOUNDARY(vaddr);
    vaddr_t end   = UPPER_PAGE_BOUNDARY(vaddr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(start);
        start += PAGE_SIZE;
    }
}

void mm_page_fault(vaddr_t vaddr)
{
    //printk("mm_page_fault(%p)\n", vaddr);
    vaddr_t page_addr = vaddr & ~PAGE_MASK;
    vaddr_t page_end  = page_addr + PAGE_SIZE;

    queue_t *q_vmr = &cur_thread->owner->vmr;
    int vmr_flag = 0;

    forlinked (node, q_vmr->head, node->next) {
        struct vmr *vmr = node->value;
        uintptr_t vmr_end = vmr->base + vmr->size;

        /* exclude non overlapping VMRs */
        if (page_end <= vmr->base || page_addr >= vmr_end)
            continue;

        //printk("vmr %p-%p\n", vmr->base, vmr_end);

        /* page overlaps vmr in at least one byte */
        vmr_flag = 1;
        mm_map(0, page_addr, PAGE_SIZE, VM_URWX);  /* FIXME */

        uintptr_t addr_start = page_addr;
        if (vmr->base > page_addr)
            addr_start = vmr->base;

        uintptr_t addr_end = page_end;
        if (vmr_end < page_end)
            addr_end = vmr_end;

        size_t size = addr_end - addr_start;
        size_t file_off = addr_start - vmr->base;

        /* File backed */
        if (vmr->flags & VM_FILE) {
            //printk("File\n");
            vfs_read(vmr->inode, vmr->off + file_off, size, (void *) addr_start);
        }

        /* Zero fill */
        if (vmr->flags & VM_ZERO) {
            //printk("Zero\n");
            memset((void *) addr_start, 0, size);
        }
    }

    if (vmr_flag) {
        //for (;;);
        return;
    }

#if 0
    if (vaddr < cur_thread->owner->heap && vaddr >= cur_thread->owner->heap_start) {
        mm_page_map(0, page_addr, VM_URWX);  /* FIXME */
        memset((void *) page_addr, 0, PAGE_SIZE);
        return;
    }
#endif

    // Send signal
    signal_proc_send(cur_thread->owner, SIGSEGV);
    return;

#if 0
    } else {


    }
#endif
}


char *kernel_heap = NULL;
void mm_setup(struct boot *boot)
{
    //printk("[0] Kernel: PMM -> Total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

    /* XXX */
    /* Fix kernel heap pointer */
    extern char *lower_kernel_heap;
    extern char *kernel_heap;
    kernel_heap = VMA(lower_kernel_heap);
    buddy_setup(boot->total_mem * 1024);
    arch_mm_setup();

    /* Setup memory regions */
    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_RESERVED) {
            size_t size = boot->mmap[i].end - boot->mmap[i].start;
            buddy_set_unusable(boot->mmap[i].start, size);
        }
    }
}
