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

#include <core/system.h>
#include <core/string.h>
#include <core/printk.h>
#include <core/chipset.h>

#include <console/earlycon.h>

#include <cpu/cpu.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <boot/multiboot.h>
#include <boot/boot.h>

#include <ds/bitmap.h>

#include <video/vbe.h>

struct cpu cpus[32];
int cpus_count;
struct boot *__kboot;

void cpu_init()
{
    earlycon_init();
    printk("x86: Welcome to AquilaOS!\n");

    printk("x86: Installing GDT\n");
    x86_gdt_setup();

    printk("x86: Installing IDT\n");
    x86_idt_setup();

    printk("x86: Installing ISRs\n");
    x86_isr_setup();

    printk("x86: Processing multiboot info block %p\n", (uintptr_t) multiboot_info);
    struct boot *boot = process_multiboot_info((multiboot_info_t *)(uintptr_t) multiboot_info);
    __kboot = boot;

    mm_setup(boot);
    kvmem_setup();

    x86_tss_sp_set(VMA(0x100000ULL));
    chipset_init();

#if 1
    //acpi_setup();
    //hpet_setup();

    //for(;;);
#endif



    //pic_setup();
    //pit_setup(20);

    extern void kmain(struct boot *);
    kmain(boot);
    
    //for (;;);
}

#if 0
struct apic_icr
{
    uint32_t vector : 8;
    uint32_t dest   : 3;
    uint32_t mode   : 1;
    uint32_t status : 1;
    uint32_t _res   : 1;
    uint32_t level  : 1;
    uint32_t triger : 1;
    uint32_t __res  : 2;
    uint32_t shorthand  : 2;
    uint32_t ___res : 12;
}__attribute__((packed));

void x86_ap_init()
{
    x86_gdt_setup();
    x86_idt_setup();

    void x86_ap_mm_setup();
    x86_ap_mm_setup();
    for(;;);
}

void x86_cpu_init()
{
    for(;;);
    early_console_init();
    x86_gdt_setup();
    x86_idt_setup();
    x86_isr_setup();

    x86_pic_setup();
    x86_pit_setup(20);

    void x86_mm_setup();
    x86_mm_setup();

    for(;;);

    extern void kmain();
    //kmain();

    void sleep(uint32_t ms);

    struct cpu_features *fp = x86_get_features();
    if(fp->apic)
    {
        printk("CPU has Local APIC\n");

        extern void trampoline();
        extern void trampoline_end();

        printk("Copying trampoline code [%d]\n", trampoline_end - trampoline);
        memcpy(0, trampoline, trampoline_end - trampoline);

        for(;;);

        extern uint32_t ap_done;
        //x86_get_cpu_count();

        printk("Init APs\n", trampoline);
        //for(;;);

        asm("sti");

        uint32_t volatile *apic = (uint32_t*) (0xFFFFF000 & (uint32_t)x86_msr_read(X86_APIC_BASE));

        apic[0xF0/4] = apic[0xF0/4] | 0x100;

        apic[0x310/4] = 1 << 24;
        apic[0x300/4] = 0x4500;

        sleep(10);

        apic[0x300/4] = 0x4600;// | ((uint32_t)(trampoline)/0x1000);

        sleep(100);

        if(ap_done > 0)
        {
            printk("AP is up\n");
            goto done;
        }

        apic[0x300/4] = 0x4600; // | ((uint32_t)(trampoline)/0x1000);

        sleep(1000);

        if(ap_done > 0)
        {
            printk("AP is up 2nd\n");
            for(;;);
        }

        done:

        asm("cli");
        x86_pic_disable();


        for(;;);

        //x86_get_cpu_count();

        //x86_apic_setup();
        //asm("sti;");

        //*(uint32_t volatile *) 0xFEE00310 = 1 << 24;
        //*(uint32_t volatile *) 0xFEE00300 = 2;// << 10;
    }

    for(;;);
    //printk("%x", x86_get_acpi_madt());

    printk("%d", x86_get_cpu_count());

    //x86_mm_setup();
    //
    //x86_idt_setup();
    for(;;);
}
#endif
