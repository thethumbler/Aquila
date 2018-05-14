#ifndef _RINGBUF_H
#define _RINGBUF_H

#define INDEX(ring, i) ((i) % ((ring)->size))

#include <stdio.h>  /* for size_t */

struct ringbuf {
    char *buf;
    size_t size;
    size_t head;
    size_t tail;
};

#define RINGBUF_NEW(sz) (&(struct ringbuf){.buf = (char[sz]){0}, .size = sz, .head = 0, .tail = 0})

static inline size_t ringbuf_read(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (ring->head == ring->tail)   /* Ring is empty */
            break;
        if (ring->head == ring->size)
            ring->head = 0;
        *buf++ = ring->buf[ring->head++];
        n--;
    }

    return size - n;
}

static inline size_t ringbuf_write(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (INDEX(ring, ring->head) == INDEX(ring, ring->tail) + 1) /* Ring is full */
            break;

        if (ring->tail == ring->size)
            ring->tail = 0;
        
        ring->buf[ring->tail++] = *buf++;
        n--;
    }

    return size - n;
}

static inline size_t ringbuf_available(struct ringbuf *ring)
{
    if (ring->tail >= ring->head)
        return ring->tail - ring->head;

    return ring->tail + ring->size - ring->head;
}

#endif
