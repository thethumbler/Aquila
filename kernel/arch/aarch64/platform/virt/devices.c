/** \file
 * The virt machine's devices that no bus finds (see `dev/platform.h`):
 * where QEMU puts them (hw/arm/virt.c), as its devicetree also says.
 */

#include <core/system.h>
#include <dev/platform.h>
#include <platform/virt.h>

/* The platform's device table: the PL011 UART. */
const struct platform_device platform_devices[] = {
    {
        .name = "pl011",
        .io   = {.type = IOADDR_MMIO8, .addr = VIRT_UART}, .size = 0x1000,
        .nirq = 1, .irq = {VIRT_UART_IRQ},
    },
    {0},
};
