/**********************************************************************
 *                  Initalization of the kernel
 *           (Setup CPU structures and memory managment)
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/printk.h>
#include <console/early_console.h>
#include <cpu/cpu.h>
#include <mm/mm.h>

#include <boot/multiboot.h>
#include <boot/boot.h>

#include <ds/bitmap.h>

struct cpu cpus[32];
int cpus_count;

void cpu_init()
{
    early_console_init();
    printk("Welcome to Aquila OS!\n");

    printk("Installing GDT\n");
    gdt_setup();

    printk("Installing IDT\n");
    idt_setup();

    printk("Installing ISRs\n");
    isr_setup();

    printk("Setting up PIC\n");
    pic_setup();

    printk("Processing multiboot info\n");
    struct boot *boot = process_multiboot_info(multiboot_info);

    printk("Setting up Physical Memory Manager (PMM)\n");
    pmm_setup(boot);

    printk("Setting up Virtual Memory Manager (VMM)\n");
    vmm_setup();

    extern volatile uint32_t *BSP_PD;
    for (int i = 0; BSP_PD[i] != 0; ++i)
        BSP_PD[i] = 0;  /* Unmap lower half */

    TLB_flush();

    set_tss_esp(VMA(0x100000));

    pic_setup();
    pit_setup(20);

    extern void kmain(struct boot *);
    kmain(boot);

#if 0
    struct cpu_features *fp = x86_get_features();
    if(fp->apic)
    {
        printk("CPU has Local APIC\n");
        extern void trampoline();
        extern void trampoline_end();

        printk("Copying trampoline code [%d]\n", (char *) trampoline_end - (char *)  trampoline);
        memcpy(0, trampoline, (char *)  trampoline_end - (char *)  trampoline);

        for(;;);
    }
#endif

    for(;;);
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
