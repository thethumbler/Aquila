#ifndef _DEV_PLATFORM_H
#define _DEV_PLATFORM_H

#include <core/types.h>
#include <cpu/io.h>

/*
 * The devices that no bus finds (PCI's devices are found by scanning the
 * bus). The platform lists where its devices are in a table
 * (arch/ARCH/platform/PLATFORM/devices.c). A driver asks for its devices
 * by name, the first one, the second one..., and attaches to each. So
 * which drivers there are is the build's choice (Build.mk), and which
 * devices the machine has, and where, is the platform's.
 */

/* The most interrupts a platform device can have. */
#define PLATFORM_IRQS   2

/** A device of the platform's table. */
struct platform_device {
    const char   *name;     /* its driver's name: "8250", "pl011", "i8042" */
    struct ioaddr io;       /* its registers: ports, or physical memory */
    size_t        size;     /* their size (memory registers are mapped) */
    int           nirq;     /* how many interrupts it has */
    unsigned      irq[PLATFORM_IRQS];
};

/* The platform's table, ended by an entry without a name. */
extern const struct platform_device platform_devices[];

/* Returns the platform's `index`th device named `name`. */
const struct platform_device *platform_device_get(const char *name, int index);

/* Gets a device's registers ready for io_in*() and io_out*(). */
int platform_device_io(const struct platform_device *pd, struct ioaddr *io);

#endif /* ! _DEV_PLATFORM_H */
