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
#include <core/panic.h>
#include <core/arch.h>
#include <boot/boot.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/buddy.h>
#include <sys/sched.h>

/* FIXME use boot time allocation scheme */
struct vm_page pages[768*1024];
#define PAGE(addr)    (pages[(addr)/PAGE_SIZE])

void mm_page_incref(paddr_t paddr)
{
    PAGE(paddr).ref++;
}

void mm_page_decref(paddr_t paddr)
{
    PAGE(paddr).ref--;
}

size_t mm_page_ref(paddr_t paddr)
{
    return PAGE(paddr).ref;
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
    if (mm_page_ref(paddr) == 0)
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
    //printk("mm_page_unmap(pmap=%p, vaddr=%p)\n", pmap, vaddr);

    /* TODO: Check out of bounds */

    /* Check if page is mapped */
    paddr_t paddr = arch_page_get_mapping(vaddr);

    //printk("paddr = %p\n", paddr);

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
    //printk("mm_map(pmap=%p, paddr=%p, vaddr=%p, size=%d, flags=%x)\n", pmap, paddr, vaddr, size, flags);

    /* TODO: Check out of bounds */

    int alloc = !paddr;

    vaddr_t endaddr = UPPER_PAGE_BOUNDARY(vaddr + size);
    vaddr = LOWER_PAGE_BOUNDARY(vaddr);
    paddr = LOWER_PAGE_BOUNDARY(paddr);

    size_t nr = (endaddr - vaddr) / PAGE_SIZE;

    while (nr--) {
        paddr_t phys = arch_page_get_mapping(vaddr);

        if (!phys) {
            if (alloc) {
                paddr = mm_page_alloc();
                //printk("paddr = %p\n", paddr);
            }
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
    //if (vaddr > KERNEL_BREAK)
    //    panic("faulting from kernel space");

    //printk("mm_page_fault(vaddr=%p, flags=%x)\n", vaddr, flags);

    vaddr_t page_addr = vaddr & ~PAGE_MASK;
    vaddr_t page_end  = page_addr + PAGE_SIZE;

    struct queue *vm_entries = &cur_thread->owner->vm_space.vm_entries;
    int vm_flag = 0;

    for (struct qnode *node = vm_entries->head; node; node = node->next) {
        struct vm_entry *vm_entry = node->value;
        uintptr_t vm_end = vm_entry->base + vm_entry->size;

        /* exclude non overlapping VM entries */
        if (page_end <= vm_entry->base || page_addr >= vm_end)
            continue;

        /* page overlaps vm_entry in at least one byte */
        vm_flag = 1;

        struct pmap *pmap = cur_thread->owner->vm_space.pmap;
        mm_map(pmap, 0, page_addr, PAGE_SIZE, VM_URWX);  /* FIXME */

        uintptr_t addr_start = page_addr;
        if (vm_entry->base > page_addr)
            addr_start = vm_entry->base;

        uintptr_t addr_end = page_end;
        if (vm_end < page_end)
            addr_end = vm_end;

        size_t size = addr_end - addr_start;
        size_t file_off = addr_start - vm_entry->base;

        /* File backed */
        if (vm_entry->flags & VM_FILE) {
            struct vm_object *vm_object = vm_entry->vm_object;
            //vfs_read(vm_entry->inode, vm_entry->off + file_off, size, (void *) addr_start);
            vfs_read(vm_object->inode, vm_entry->off + file_off, size, (void *) addr_start);
        }

        /* Zero fill */
        if (vm_entry->flags & VM_ZERO)
            memset((void *) addr_start, 0, size);
    }

    if (vm_flag) {
        return;
    }

    printk("mm_page_fault(vaddr=%p, flags=%x)\n", vaddr, flags);
    signal_proc_send(cur_thread->owner, SIGSEGV);
    return;
}

void mm_setup(struct boot *boot)
{
    printk("kernel: Total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

    buddy_setup(boot->total_mem * 1024);

    /* Setup memory regions */
    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_RESERVED) {
            size_t size = boot->mmap[i].end - boot->mmap[i].start;
            buddy_set_unusable(boot->mmap[i].start, size);
        }
    }

    /* Mark modules space as unusable */
    for (int i = 0; i < boot->modules_count; ++i) {
        module_t *mod = &boot->modules[i];
        buddy_set_unusable(LMA((uintptr_t) mod->addr), mod->size);
    }

    arch_mm_setup();
}
