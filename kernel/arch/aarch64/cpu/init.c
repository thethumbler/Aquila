/** \file
 * aarch64: setting up the kernel, from where boot/head.S jumps to, up to
 * kmain(), in the order that docs/kernel/booting.md's table gives (as
 * x86's x86_cpu_init() does).
 */

#include <boot/boot.h>
#include <console/earlycon.h>
#include <core/arch.h>
#include <core/kargs.h>
#include <core/printk.h>
#include <cpu/cpu.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <sys/cpu.h>
#include <sys/lock.h>
#include <platform/virt.h>

struct boot *__kboot;

/* The boot CPU's idle loop's stack. */
static char bsp_idle_stack[8192] __aligned(16);

/* User mode's use of the FP/SIMD registers traps (CPACR_EL1.FPEN = 0b01)
 * until cpu/fpsimd.c gives them to its thread; the kernel uses none. */
#define CPACR_FPEN_TRAP     (1UL << 20)

/**
 * Sets up the CPU, memory and the platform, then calls `kmain()`.
 * `boot/head.S` jumps here once the MMU is on. It doesn't return.
 *
 * @param dtb  the devicetree's physical address, from the boot loader
 */
void aarch64_init(uintptr_t dtb)
{
    /* the boot CPU's struct cpu first: curcpu() and curthread need it */
    struct cpu *bsp = &cpus[0];
    bsp->self = bsp;
    bsp->id = 0;
    bsp->arch.idle_stack = (uintptr_t) bsp_idle_stack + sizeof(bsp_idle_stack);
    cpus_count = 1;
    write_sysreg(tpidr_el1, bsp);

    /* the kernel lock is the kernel's until the first process runs */
    kernel_lock();

    earlycon_init();
    printk("aarch64: Welcome to AquilaOS!\n");
    printk("aarch64: running at EL%d, the devicetree at %p\n",
           (int) (read_sysreg(CurrentEL) >> 2), (void *) dtb);

    /* the exception vectors, so that an exception is reported rather than
     * hanging the CPU */
    extern char aarch64_vectors[];
    write_sysreg(vbar_el1, aarch64_vectors);
    write_sysreg(cpacr_el1, CPACR_FPEN_TRAP);
    isb();

    struct boot *boot = fdt_boot(dtb);
    __kboot = boot;

    mm_setup(boot);
    kvmem_setup();

    kargs_parse(boot->cmdline);
    earlycon_reinit();

    platform_init();

    extern void kmain(struct boot *);
    kmain(boot);
}
