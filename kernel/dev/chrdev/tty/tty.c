#include <core/system.h>
#include <dev/ttydev.h>
#include <fs/posix.h>

static inline struct dev *ttydev_mux(struct devid *dd)
{
    switch (dd->minor) {
        case 0: /* /dev/tty */
            // return ttydev
            break;
        case 1: /* /dev/console */
            return &condev;
            break;
        case 2: /* /dev/ptmx */
            return &ptmdev;
            break;
    }

    return NULL;
}

static int ttydev_probe()
{
    kdev_chrdev_register(5, &ttydev);
    return 0;
}

struct dev ttydev = {
    .name  = "ttydev",
    .probe = ttydev_probe,
    .mux   = ttydev_mux,
};

MODULE_INIT(ttydev, ttydev_probe, NULL);
