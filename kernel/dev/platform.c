/** \file
 * The platform's devices: those that no bus can find (see
 * `dev/platform.h`).
 *
 * Each platform lists them in its own table, `platform_devices[]`, and a
 * driver looks up the devices it drives there by name.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <dev/platform.h>

/**
 * Finds one of the platform's devices by its driver's name.
 *
 * @param name   the driver's name (`"8250"`, `"pl011"`, `"i8042"`, ...)
 * @param index  which of the devices with that name: 0 for the first
 * @return the device, or NULL if there are not that many
 */
const struct platform_device *platform_device_get(const char *name, int index)
{
    for (const struct platform_device *pd = platform_devices; pd->name; ++pd)
        if (!strcmp(pd->name, name) && !index--)
            return pd;

    return NULL;
}

/**
 * Gets a device's registers, ready for `io_in8()` and the others. Ports
 * are used as they are; memory-mapped registers are mapped first.
 *
 * @param pd  the device
 * @param io  set to its registers
 * @return 0, or `-ENOMEM` if they couldn't be mapped
 */
int platform_device_io(const struct platform_device *pd, struct ioaddr *io)
{
    *io = pd->io;

    if (io->type == IOADDR_PORT)
        return 0;

    void *regs = arch_mmio_map(pd->io.addr, pd->size);

    if (!regs)
        return -ENOMEM;

    io->addr = (uintptr_t) regs;
    return 0;
}
