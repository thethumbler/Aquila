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
#include <core/string.h>
#include <core/panic.h>
#include <cpu/cpu.h>
#include <boot/multiboot.h>
#include <boot/boot.h>
#include <mm/mm.h>

void arch_mm_setup()
{
    /* Fix kernel heap pointer */
    extern char *lower_kernel_heap;
    extern char *kernel_heap;
    kernel_heap = VMA(lower_kernel_heap);

#if ARCH_BITS==64
    /* FIXME */
    extern void setup_64_bit_paging();
    setup_64_bit_paging();
    return;
#endif

    //struct cpu_features features;
    //get_cpu_features(&features);

#if !defined(X86_PAE) || !X86_PAE
    extern void setup_32_bit_paging(void);
    setup_32_bit_paging();
    //if (features.pse) {
    //    //write_cr4(read_cr4() | CR4_PSE);
    //    printk("[0] Kernel: PMM -> Found PSE support\n");
    //}
#else   /* PAE */
    panic("PAE Not supported");
    if (features.pae) {
        printk("[0] Kernel: PMM -> Found PAE support\n");
    } else if(features.pse) {
        printk("[0] Kernel: PMM -> Found PSE support\n");
    }
#endif
}
