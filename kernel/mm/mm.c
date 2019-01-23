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

struct vm_page pages[768*1024];
#define PAGE(addr)    (pages[(addr)/PAGE_SIZE])

void mm_page_incref(paddr_t paddr)
{
    PAGE(paddr).refcnt++;
}

void mm_page_decref(paddr_t paddr)
{
    PAGE(paddr).refcnt--;
}

size_t mm_page_refcnt(paddr_t paddr)
{
    return PAGE(paddr).refcnt;
}

paddr_t mm_page_alloc(void)
{
    /* Get new frame */
    return buddy_alloc(BUDDY_ZONE_NORMAL, PAGE_SIZE);
}

void mm_page_dealloc(paddr_t paddr)
{
    /* TODO: Check out of bounds */

    /* Release frame if it is no longer referenced */
    if (mm_page_refcnt(paddr) == 0)
        buddy_free(BUDDY_ZONE_NORMAL, paddr, PAGE_SIZE);
}

int mm_page_map(struct pmap *pmap, vaddr_t vaddr, paddr_t paddr, int flags)
{
    /* TODO: Check out of bounds */

    /* Increment references count to physical page */
    mm_page_incref(paddr);

    return arch_pmap_add(pmap, vaddr, paddr, flags);
}

int mm_page_unmap(struct pmap *pmap, vaddr_t vaddr)
{
    /* TODO: Check out of bounds */

    /* Check if page is mapped */
    paddr_t paddr = arch_page_get_mapping(vaddr);

    if (paddr) {
        /* Decrement references count to physical page */
        mm_page_decref(paddr);

        /* Call arch specific page unmapper */
        arch_pmap_remove(pmap, vaddr, vaddr + PAGE_SIZE);

        /* Release page -- checks ref count */
        mm_page_dealloc(paddr);
        return 0;
    }

    return -EINVAL;
}

int mm_map(struct pmap *pmap, paddr_t paddr, vaddr_t vaddr, size_t size, int flags)
{
    //printk("mm_map(pmap=%p, pa=%p, va=%p, size=%d, flags=%x)\n", pmap, paddr, vaddr, size, flags);

    /* TODO: Check out of bounds */

    int alloc = !paddr;

    vaddr_t endaddr = UPPER_PAGE_BOUNDARY(vaddr + size);
    vaddr = LOWER_PAGE_BOUNDARY(vaddr);
    paddr = LOWER_PAGE_BOUNDARY(paddr);

    size_t nr = (endaddr - vaddr) / PAGE_SIZE;

    while (nr--) {
        paddr_t phys = arch_page_get_mapping(vaddr);

        if (!phys) {
            if (alloc)
                paddr = mm_page_alloc();
            mm_page_map(pmap, vaddr, paddr, flags);
        }

        vaddr += PAGE_SIZE;
        paddr += PAGE_SIZE;
    }

    return 0;
}

void mm_unmap(struct pmap *pmap, vaddr_t vaddr, size_t size)
{
    /* TODO: Check out of bounds */

    if (size < PAGE_SIZE) return;

    vaddr_t sva = UPPER_PAGE_BOUNDARY(vaddr);
    vaddr_t eva = LOWER_PAGE_BOUNDARY(vaddr + size);

    //arch_pmap_remove(pmap, sva, eva);

#if 1
    size_t nr = (eva - sva)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(pmap, sva);
        sva += PAGE_SIZE;
    }
#endif
}

void mm_unmap_full(struct pmap *pmap, vaddr_t vaddr, size_t size)
{
    /* TODO: Check out of bounds */

    vaddr_t start = LOWER_PAGE_BOUNDARY(vaddr);
    vaddr_t end   = UPPER_PAGE_BOUNDARY(vaddr + size);

    size_t nr = (end - start)/PAGE_SIZE;

    while (nr--) {
        mm_page_unmap(pmap, start);
        start += PAGE_SIZE;
    }
}

void mm_page_fault(vaddr_t vaddr, int flags)
{
    /* TODO: Check out of bounds */

    vaddr_t page_addr = vaddr & ~PAGE_MASK;
    vaddr_t page_end  = page_addr + PAGE_SIZE;

    struct queue *qvm_entries = &cur_thread->owner->vm_space.vm_entries;
    int vm_flag = 0;

    forlinked (node, qvm_entries->head, node->next) {
        struct vm_entry *vm_entry = node->value;
        uintptr_t vmr_end = vm_entry->base + vm_entry->size;

        /* exclude non overlapping VMRs */
        if (page_end <= vm_entry->base || page_addr >= vmr_end)
            continue;

        /* page overlaps vm_entry in at least one byte */
        vm_flag = 1;
        struct pmap *pmap = cur_thread->owner->vm_space.pmap;
        mm_map(pmap, 0, page_addr, PAGE_SIZE, VM_URWX);  /* FIXME */

        uintptr_t addr_start = page_addr;
        if (vm_entry->base > page_addr)
            addr_start = vm_entry->base;

        uintptr_t addr_end = page_end;
        if (vmr_end < page_end)
            addr_end = vmr_end;

        size_t size = addr_end - addr_start;
        size_t file_off = addr_start - vm_entry->base;

        /* File backed */
        if (vm_entry->flags & VM_FILE)
            vfs_read(vm_entry->inode, vm_entry->off + file_off, size, (void *) addr_start);

        /* Zero fill */
        if (vm_entry->flags & VM_ZERO)
            memset((void *) addr_start, 0, size);
    }

    if (vm_flag) {
        return;
    }

    printk("mm_page_fault(vaddr=%p, flags=%x)\n", vaddr, flags);
    for (;;);

    signal_proc_send(cur_thread->owner, SIGSEGV);
    return;
}

char *kernel_heap = NULL;
void mm_setup(struct boot *boot)
{
    printk("kernel: Total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

    /* XXX */
    /* Fix kernel heap pointer */
    extern char *lower_kernel_heap;
    extern char *kernel_heap;
    kernel_heap = VMA(lower_kernel_heap);
    buddy_setup(boot->total_mem * 1024);

    /* Setup memory regions */
    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_RESERVED) {
            size_t size = boot->mmap[i].end - boot->mmap[i].start;
            buddy_set_unusable(boot->mmap[i].start, size);
        }
    }

    arch_mm_setup();
}
