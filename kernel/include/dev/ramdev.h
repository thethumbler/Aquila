#ifndef _DEV_RAMDEV_H
#define _DEV_RAMDEV_H

#include <core/system.h>

struct ramdev_priv {
    void *addr;
};

#include <dev/dev.h>

extern struct dev ramdev;

#endif /* ! _DEV_RAMDEV_H */
