/** \file
 * The PC's devices that no bus finds (see `dev/platform.h`), at the ports
 * and ISA interrupts that every PC has them on.
 */

#include <core/system.h>
#include <dev/platform.h>

const struct platform_device platform_devices[] = {
    /* the serial ports, COM1 and COM2 (the driver checks that they are
     * there) */
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
    /* the keyboard controller, with the keyboard's interrupt and the
     * mouse's */
    {
        .name = "i8042",
        .io   = {.type = IOADDR_PORT, .addr = 0x60}, .size = 5,
        .nirq = 2, .irq = {1, 12},
    },
    {0},
};
