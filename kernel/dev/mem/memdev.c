#include <core/system.h>
#include <core/module.h>
#include <dev/dev.h>

#include <memdev.h>

struct dev memdev;

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
            //return &portdev;
            break;
        case 5: /* /dev/zero */
            return &zerodev;
        case 6: /* /dev/core */
            break;
        case 7: /* /dev/full */
            break;
        case 8: /* /dev/random */
            break;
        case 9: /* /dev/urandom */
            break;
        case 10: /* /dev/aio */
            break;
        case 11: /* /dev/kmsg */
            return &kmsgdev;
    }

    return NULL;
}

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
