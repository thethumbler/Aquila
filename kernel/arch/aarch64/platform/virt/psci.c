/** \file
 * Restarting and powering off: PSCI's calls to the firmware (QEMU's), by
 * hvc, as the virt machine's devicetree says (/psci: method "hvc")
 *
 * References:
 *      [1] - Arm Power State Coordination Interface (DEN 0022)
 */

#include <core/system.h>
#include <core/arch.h>

#define PSCI_SYSTEM_OFF     0x84000008
#define PSCI_SYSTEM_RESET   0x84000009

static void psci(uint64_t fn)
{
    register uint64_t x0 asm("x0") = fn;
    asm volatile("hvc #0" : "+r"(x0) :: "memory");
}

void arch_reboot(void)
{
    psci(PSCI_SYSTEM_RESET);
}

void arch_poweroff(void)
{
    psci(PSCI_SYSTEM_OFF);
    arch_halt();
}

void arch_halt(void)
{
    arch_interrupts_disable();

    for (;;)
        asm volatile("wfi");
}
