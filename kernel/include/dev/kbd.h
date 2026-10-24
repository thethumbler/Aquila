#ifndef _DEV_KBD_H
#define _DEV_KBD_H

#include <core/system.h>
#include <ds/ringbuf.h>

struct keyboard {
    const char     *name;

    struct waitq   *read_queue;
    struct ringbuf *ring;
};

#endif /* ! _DEV_KBD_H */
