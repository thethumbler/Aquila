/** \file
 * The PC's devices no bus finds (dev/platform.h): at the ports and ISA
 * interrupts every PC has them
 */

#include <core/system.h>
#include <dev/platform.h>

const struct platform_device platform_devices[] = {
    /* the serial ports: COM1, COM2 (if they're there: the driver looks) */
    {
        .name = "8250",
        .io   = {.type = IOADDR_PORT, .addr = 0x3F8}, .size = 8,
        .nirq = 1, .irq = {4},
    },
    {
        .name = "8250",
        .io   = {.type = IOADDR_PORT, .addr = 0x2F8}, .size = 8,
        .nirq = 1, .irq = {3},
    },
    /* the keyboard controller: the keyboard's interrupt, the mouse's */
    {
        .name = "i8042",
        .io   = {.type = IOADDR_PORT, .addr = 0x60}, .size = 5,
        .nirq = 2, .irq = {1, 12},
    },
    {0},
};
