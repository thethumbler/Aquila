#ifndef _DEV_PLATFORM_H
#define _DEV_PLATFORM_H

#include <core/types.h>
#include <cpu/io.h>

/*
 * The devices no bus finds (PCI's are scanned for): where the platform
 * has them, in its table (arch/ARCH/platform/PLATFORM/devices.c). A
 * driver asks for its devices by name, the first, the second... and
 * attaches to each: which drivers there are is the build's (Build.mk),
 * which devices the machine has, and where, the platform's.
 */

#define PLATFORM_IRQS   2

struct platform_device {
    const char   *name;     /* its driver's: "8250", "pl011", "i8042" */
    struct ioaddr io;       /* its registers: ports, or physical memory */
    size_t        size;     /* how much of them (memory's is mapped) */
    int           nirq;     /* its interrupts */
    unsigned      irq[PLATFORM_IRQS];
};

/* the platform's table, ended by one without a name */
extern const struct platform_device platform_devices[];

/* the `index'th device named `name', NULL past the last */
const struct platform_device *platform_device_get(const char *name, int index);

/* its registers, for io_in*() and io_out*() (memory mapped); -1 if they
 * can't be */
int platform_device_io(const struct platform_device *pd, struct ioaddr *io);

#endif /* ! _DEV_PLATFORM_H */
