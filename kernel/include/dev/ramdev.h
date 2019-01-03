#ifndef _RAMDEV_H
#define _RAMDEV_H

#include <core/system.h>

struct ramdev_priv {
    void *addr;
};

#include <dev/dev.h>

extern struct dev ramdev;

#endif /* ! _RAMDEV_H */
