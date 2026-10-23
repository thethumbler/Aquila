/** \file
 * QEMU's virt machine: its interrupt controller
 */

#include <core/system.h>
#include <platform/virt.h>

int platform_init(void)
{
    gic_setup();
    printk("virt: GICv2, interrupts through it\n");
    return 0;
}
