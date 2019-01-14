/**********************************************************************
 *                  Initalization of the kernel
 *           (Setup CPU structures and memory managment)
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <boot/boot.h>
#include <boot/multiboot.h>
#include <console/earlycon.h>
#include <core/kargs.h>
#include <core/platform.h>
#include <core/printk.h>
#include <core/string.h>
#include <cpu/cpu.h>
#include <ds/bitmap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <video/vbe.h>

struct x86_cpu cpus[32];
int cpus_count;
struct boot *__kboot;

void cpu_init()
{
    earlycon_init();
    printk("x86: Welcome to AquilaOS!\n");

    //assert_sizeof(struct x86_regs, 12*4);

    printk("x86: Installing GDT\n");
    x86_gdt_setup();
    x86_tss_setup(VMA(0x100000ULL));

    printk("x86: Installing IDT\n");
    x86_idt_setup();

    printk("x86: Installing ISRs\n");
    x86_isr_setup();

    printk("x86: Processing multiboot info block %p\n", (uintptr_t) multiboot_info);
    struct boot *boot = process_multiboot_info((multiboot_info_t *)(uintptr_t) multiboot_info);
    __kboot = boot;

    mm_setup(boot);
    kvmem_setup();

    /* parse command line */
    kargs_parse(boot->cmdline);

    /* reinit early console */
    earlycon_reinit();

    platform_init();

    extern void kmain(struct boot *);
    kmain(boot);
}
