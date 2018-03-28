#ifndef _RAMDEV_H
#define _RAMDEV_H

#include <core/system.h>
#include <dev/dev.h>

typedef struct
{
    void *addr;
} ramdev_private_t;

extern struct dev ramdev;

#endif /* !_RAMDEV_H */
