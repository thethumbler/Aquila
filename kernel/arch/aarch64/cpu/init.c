/** \file
 * aarch64: the kernel set up, from head.S's jump to kmain(), in the
 * order booting.md's table gives (as x86's x86_cpu_init())
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

/* the boot CPU's idle loop's */
static char bsp_idle_stack[8192] __aligned(16);

/* the FP/SIMD registers trapped (CPACR_EL1.FPEN 0): the kernel uses none,
 * user mode none yet (cpu/trap.c) */
#define CPACR_FPEN_TRAP     0

void aarch64_init(uintptr_t dtb)
{
    /* the boot CPU's own, first (curcpu(), curthread) */
    struct cpu *bsp = &cpus[0];
    bsp->self = bsp;
    bsp->id = 0;
    bsp->arch.idle_stack = (uintptr_t) bsp_idle_stack + sizeof(bsp_idle_stack);
    cpus_count = 1;
    write_sysreg(tpidr_el1, bsp);

    /* the kernel's, until the first process runs */
    kernel_lock();

    earlycon_init();
    printk("aarch64: Welcome to AquilaOS!\n");
    printk("aarch64: running at EL%d, the devicetree at %p\n",
           (int) (read_sysreg(CurrentEL) >> 2), (void *) dtb);

    /* exceptions reported, not a hang */
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
