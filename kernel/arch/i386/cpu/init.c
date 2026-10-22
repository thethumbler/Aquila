/**********************************************************************
 *                  Initalization of the kernel
 *           (Setup CPU structures and memory managment)
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
#include <sys/cpu.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <video/vbe.h>

struct boot *__kboot;

/* the boot CPU's idle loop's */
static char bsp_idle_stack[8192] __aligned(16);

void x86_cpu_init(void)
{
    /* the boot CPU's own, first (curcpu(), curthread) */
    struct cpu *bsp = &cpus[0];
    bsp->id = 0;
    bsp->arch.idle_stack = (uintptr_t) bsp_idle_stack + sizeof(bsp_idle_stack);
    cpus_count = 1;

    x86_gdt_setup(bsp, VMA(0x100000ULL));

    earlycon_init();
    printk("x86: Welcome to AquilaOS!\n");

    printk("x86: installing IDT\n");
    x86_idt_setup();

    printk("x86: installing ISRs\n");
    x86_isr_setup();

    x86_fpu_setup();

    printk("x86: processing multiboot info block at %p\n", (uintptr_t) multiboot_info);
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
