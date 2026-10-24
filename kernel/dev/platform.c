/** \file
 * The platform's devices, those no bus finds: dev/platform.h
 */

#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>
#include <dev/platform.h>

const struct platform_device *platform_device_get(const char *name, int index)
{
    for (const struct platform_device *pd = platform_devices; pd->name; ++pd)
        if (!strcmp(pd->name, name) && !index--)
            return pd;

    return NULL;
}

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
