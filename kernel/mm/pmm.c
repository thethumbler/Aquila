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
#include <core/qsort.h>
#include <boot/boot.h>
#include <mm/mm.h>
#include <mm/buddy.h>
#include <sys/sched.h>

char *kernel_heap = NULL;

void pmm_setup(struct boot *boot)
{
    //printk("[0] Kernel: PMM -> Total memory: %d KiB, %d MiB\n", boot->total_mem, boot->total_mem / 1024);

    /* XXX */
    /* Fix kernel heap pointer */
    extern char *lower_kernel_heap;
    extern char *kernel_heap;
    kernel_heap = VMA(lower_kernel_heap);
    buddy_setup(boot->total_mem * 1024);
    arch_pmm_setup();

    /* Setup memory regions */
    for (int i = 0; i < boot->mmap_count; ++i) {
        if (boot->mmap[i].type == MMAP_RESERVED) {
            size_t size = boot->mmap[i].end - boot->mmap[i].start;
            buddy_set_unusable(boot->mmap[i].start, size);
        }
    }
}

void pmm_lazy_alloc(uintptr_t addr)
{
    if (addr < cur_thread->owner->heap) {
        pmman.map(addr & ~PAGE_MASK, PAGE_SIZE, VM_URW); 
    } else {
        /* Error */
    }
}
