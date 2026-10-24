/** \file
 * The memory devices: the character devices of major 1, as on Linux.
 *
 * Each minor is a device of its own, with its own driver: `/dev/null`
 * (3), `/dev/zero` (5), `/dev/random` and `/dev/urandom` (8, 9) and
 * `/dev/kmsg` (11). `memdev_mux()` picks the driver by minor. The other
 * minors Linux has (`/dev/mem`, `/dev/port`, `/dev/full`, ...) aren't
 * there.
 */

#include <core/system.h>
#include <core/module.h>
#include <dev/dev.h>

#include <memdev.h>

struct dev memdev;

/**
 * Picks the driver of a memory device, by its minor.
 *
 * @param dd  the device's number
 * @return its driver, or NULL for a minor that has none
 */
struct dev *memdev_mux(struct devid *dd)
{
    switch (dd->minor) {
        case 0:
        case 1: /* /dev/mem */
        case 2: /* /dev/kmem */
            break;
        case 3: /* /dev/null */
            return &nulldev;
        case 4: /* /dev/port */
            break;
        case 5: /* /dev/zero */
            return &zerodev;
        case 6: /* /dev/core */
            break;
        case 7: /* /dev/full */
            break;
        case 8: /* /dev/random */
        case 9: /* /dev/urandom */
            return &randomdev;
        case 10: /* /dev/aio */
            break;
        case 11: /* /dev/kmsg */
            return &kmsgdev;
    }

    return NULL;
}

/**
 * Registers the memory devices, as character devices of major 1.
 *
 * @return 0
 */
int memdev_probe()
{
    kdev_chrdev_register(1, &memdev);
    return 0;
}

struct dev memdev = {
    .name = "memdev",
    .mux  = memdev_mux,
};

MODULE_INIT(memdev, memdev_probe, NULL)
