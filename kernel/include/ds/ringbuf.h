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

/*
 * head and tail are always kept in [0, size), the ring is empty when
 * head == tail and full when tail is right behind head, so a ring can
 * hold at most size - 1 bytes.
 */

static inline size_t ringbuf_available(struct ringbuf *ring)
{
    return RING_INDEX(ring, ring->tail + ring->size - ring->head);
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
        *buf++ = ring->buf[ring->head];
        ring->head = RING_INDEX(ring, ring->head + 1);
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
    size_t available = ringbuf_available(ring);

    if (off < 0 || (size_t) off >= available)
        return 0;

    n = MIN(n, available - (size_t) off);

    size_t head = RING_INDEX(ring, ring->head + (size_t) off);

    for (size_t i = 0; i < n; ++i) {
        *buf++ = ring->buf[head];
        head = RING_INDEX(ring, head + 1);
    }

    return n;
}

/* the first `n' bytes go, unread (at most what there is) */
static inline void ringbuf_discard(struct ringbuf *ring, size_t n)
{
    n = MIN(n, ringbuf_available(ring));
    ring->head = RING_INDEX(ring, ring->head + n);
}

static inline size_t ringbuf_write(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (RING_INDEX(ring, ring->tail + 1) == ring->head) /* Ring is full */
            break;

        ring->buf[ring->tail] = *buf++;
        ring->tail = RING_INDEX(ring, ring->tail + 1);
        n--;
    }

    return size - n;
}

static inline size_t ringbuf_write_overwrite(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (RING_INDEX(ring, ring->tail + 1) == ring->head) {
            /* Ring is full, drop the oldest byte */
            ring->head = RING_INDEX(ring, ring->head + 1);
        }

        ring->buf[ring->tail] = *buf++;
        ring->tail = RING_INDEX(ring, ring->tail + 1);
        n--;
    }

    return size - n;
}

#endif /* ! _DS_RINGBUF_H */
