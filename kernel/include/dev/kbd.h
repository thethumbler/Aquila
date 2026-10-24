#ifndef _DEV_KBD_H
#define _DEV_KBD_H

#include <core/system.h>
#include <ds/ringbuf.h>

/** A keyboard: the bytes it sent, waiting to be read. */
struct keyboard {
    const char     *name;

    struct waitq   *read_queue;
    struct ringbuf *ring;
};

#endif /* ! _DEV_KBD_H */
