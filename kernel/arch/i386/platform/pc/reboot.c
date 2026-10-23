#include <core/system.h>
#include <core/arch.h>
#include <platform/misc.h>

void platform_reboot(void)
{
    x86_i8042_reboot();
}

void arch_reboot(void)
{
    platform_reboot();
}

void arch_halt(void)
{
    for (;;) {
        arch_interrupts_disable();
        asm volatile ("hlt");
    }
}

/** the machine stays halted without ACPI */
void arch_poweroff(void)
{
    if (x86_acpi_poweroff)
        x86_acpi_poweroff();

    arch_halt();
}
