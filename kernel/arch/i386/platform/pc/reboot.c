/** \file
 * The PC: restarting, halting and powering off.
 */

#include <core/system.h>
#include <core/arch.h>
#include <platform/misc.h>
#include <dev/i8042.h>

/** Restarts the machine, through the keyboard controller, which can reset
 * the CPU. */
void platform_reboot(void)
{
#ifdef CONFIG_DEV_I8042
    i8042_reboot();
#endif
}

/* Restarts the machine. */
void arch_reboot(void)
{
    platform_reboot();
}

/* Stops the CPU, with interrupts off, for good. */
void arch_halt(void)
{
    for (;;) {
        arch_interrupts_disable();
        asm volatile ("hlt");
    }
}

/* Powers the machine off with ACPI; without ACPI, it stays halted. */
void arch_poweroff(void)
{
    if (x86_acpi_poweroff)
        x86_acpi_poweroff();

    arch_halt();
}
