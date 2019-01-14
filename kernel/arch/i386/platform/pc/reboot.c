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
