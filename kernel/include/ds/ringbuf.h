#ifndef _RINGBUF_H
#define _RINGBUF_H

#include <core/system.h>
#include <mm/mm.h>

#define INDEX(ring, i) ((i) % ((ring)->size))

struct ringbuf {
    char *buf;
    size_t size;
    size_t head;
    size_t tail;
};

#define RINGBUF_NEW(sz) (&(struct ringbuf){.buf = (char[sz]){0}, .size = sz, .head = 0, .tail = 0})

static inline struct ringbuf *ringbuf_new(size_t size)
{
    struct ringbuf *ring = kmalloc(sizeof(struct ringbuf));
    *ring = (struct ringbuf) {
        .buf = kmalloc(size),
        .size = size,
        .head = 0,
        .tail = 0
    };
    return ring;
}

static inline void ringbuf_free(struct ringbuf *r)
{
    kfree(r->buf);
    kfree(r);
}

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

static inline size_t ringbuf_read_noconsume(struct ringbuf *ring, off_t off, size_t n, char *buf)
{
    size_t size = n;
    size_t head = ring->head + off;

    if (ring->head < ring->tail && head > ring->tail)
        return 0;

    while (n) {
        if (head == ring->size)
            head = 0;
        if (head == ring->tail)   /* Ring is empty */
            break;
        *buf++ = ring->buf[head++];
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

static inline size_t ringbuf_write_overwrite(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (INDEX(ring, ring->head) == INDEX(ring, ring->tail) + 1) {
            /* move head to match */
            ring->head = INDEX(ring, ring->head) + 1;
        }

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

#endif /* ! _RINGBUF_H */
