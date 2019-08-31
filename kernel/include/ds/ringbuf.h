#ifndef _DS_RINGBUF_H
#define _DS_RINGBUF_H

#include <core/system.h>
#include <mm/mm.h>

#define RING_INDEX(ring, i) ((i) % ((ring)->size))

/**
 * \ingroup ds
 * \brief ring buffer
 */
struct ringbuf {
    char *buf;
    size_t size;
    size_t head;
    size_t tail;
};

/**
 * \ingroup ds
 * \brief create a new statically allocated ring buffer
 */
#define RINGBUF_NEW(sz) (&(struct ringbuf){.buf = (char[sz]){0}, .size = sz, .head = 0, .tail = 0})

/**
 * \ingroup ds
 * \brief create a new dynamically allocated ring buffer
 */
static inline struct ringbuf *ringbuf_new(size_t size)
{
    struct ringbuf *ring = kmalloc(sizeof(struct ringbuf), &M_BUFFER, 0);

    if (!ring)
        return NULL;

    ring->buf  = kmalloc(size, &M_BUFFER, 0);

    if (!ring->buf) {
        kfree(ring);
        return NULL;
    }

    ring->size = size;
    ring->head = 0;
    ring->tail = 0;

    return ring;
}

/**
 * \ingroup ds
 * \brief free a dynamically allocated ring buffer
 */
static inline void ringbuf_free(struct ringbuf *r)
{
    if (!r)
        return;

    kfree(r->buf);
    kfree(r);
}

/** 
 * \ingroup ds
 * \brief read from a ring buffer
 */
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

/** 
 * \ingroup ds
 * \brief peek (non-destructive read) from a ring buffer
 */
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
        if (RING_INDEX(ring, ring->head) == RING_INDEX(ring, ring->tail) + 1) /* Ring is full */
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
        if (RING_INDEX(ring, ring->head) == RING_INDEX(ring, ring->tail) + 1) {
            /* move head to match */
            ring->head = RING_INDEX(ring, ring->head) + 1;
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

#endif /* ! _DS_RINGBUF_H */
