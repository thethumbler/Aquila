#ifndef _DEV_RAMDEV_H
#define _DEV_RAMDEV_H

#include <core/system.h>

/* The RAM disk (dev/rd/ramdisk.c): the boot loader's module, as a block
 * device. */

/** A RAM disk's memory. */
struct ramdev_priv {
    void *addr;
};

#include <dev/dev.h>

extern struct dev rddev;

#endif /* ! _DEV_RAMDEV_H */
