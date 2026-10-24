#ifndef _DS_RINGBUF_H
#define _DS_RINGBUF_H

#include <core/system.h>
#include <mm/mm.h>
#include <fs/iobuf.h>

/*
 * Ring buffers: a fixed-size buffer of bytes, written at its tail and read
 * at its head, both wrapping around. A terminal's input and output, a
 * pipe's data and the kernel's messages are kept in one.
 */

/* An index wrapped around into the ring. */
#define RING_INDEX(ring, i) ((i) % ((ring)->size))

/** A ring buffer. */
struct ringbuf {
    char *buf;
    size_t size;
    size_t head;
    size_t tail;
};

/* A new static ring buffer of `sz` bytes. */
#define RINGBUF_NEW(sz) (&(struct ringbuf){.buf = (char[sz]){0}, .size = sz, .head = 0, .tail = 0})

/** Returns a new ring buffer of `size` bytes, from kmalloc(), or NULL if
 * there was no memory for it. */
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

/** Frees a ring buffer that ringbuf_new() made (NULL does nothing). */
static inline void ringbuf_free(struct ringbuf *r)
{
    if (!r)
        return;

    kfree(r->buf);
    kfree(r);
}

/*
 * The head and the tail are always in [0, size). The ring is empty when
 * head == tail, and full when the tail is right behind the head, so a
 * ring can hold at most size - 1 bytes.
 */

/** Returns how many bytes the ring holds. */
static inline size_t ringbuf_available(struct ringbuf *ring)
{
    return RING_INDEX(ring, ring->tail + ring->size - ring->head);
}

/** Reads up to `n` bytes from the ring into `buf`, and returns how many
 * there were. */
static inline size_t ringbuf_read(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (ring->head == ring->tail)   /* the ring is empty */
            break;
        *buf++ = ring->buf[ring->head];
        ring->head = RING_INDEX(ring, ring->head + 1);
        n--;
    }

    return size - n;
}

/** Copies up to `n` bytes from the ring, starting `off` bytes from its
 * head, without taking them off it. Returns how many were copied. */
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

/** Throws away the first `n` bytes of the ring, unread (at most what it
 * holds). */
static inline void ringbuf_discard(struct ringbuf *ring, size_t n)
{
    n = MIN(n, ringbuf_available(ring));
    ring->head = RING_INDEX(ring, ring->head + n);
}

/** Writes up to `n` bytes from `buf` into the ring, as many as there is
 * room for, and returns how many. */
static inline size_t ringbuf_write(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (RING_INDEX(ring, ring->tail + 1) == ring->head) /* the ring is full */
            break;

        ring->buf[ring->tail] = *buf++;
        ring->tail = RING_INDEX(ring, ring->tail + 1);
        n--;
    }

    return size - n;
}

/*
 * Copying between a ring and an iobuf (a read()'s or a write()'s buffer
 * in user memory): up to n bytes, one contiguous piece of the ring at a
 * time. A piece is taken off (or added to) the ring only once it has been
 * copied, so a bad address loses nothing. They return how many bytes were
 * copied, or -EFAULT if none could be.
 */

/** Reads up to `n` bytes from the ring into an iobuf. */
static inline ssize_t ringbuf_read_iobuf(struct ringbuf *ring, size_t n, struct iobuf *io)
{
    size_t done = 0;

    n = MIN(n, io->resid);

    while (done < n && ring->head != ring->tail) {
        /* up to the tail, or the end of the ring */
        size_t end = ring->tail > ring->head ? ring->tail : ring->size;
        ssize_t r = iobuf_out(io, ring->buf + ring->head, MIN(n - done, end - ring->head));

        if (r < 0)
            return done ? (ssize_t) done : r;

        ring->head = RING_INDEX(ring, ring->head + r);
        done += r;
    }

    return done;
}

/** Writes up to `n` bytes from an iobuf into the ring. */
static inline ssize_t ringbuf_write_iobuf(struct ringbuf *ring, size_t n, struct iobuf *io)
{
    size_t done = 0;

    n = MIN(n, io->resid);

    while (done < n && RING_INDEX(ring, ring->tail + 1) != ring->head) {
        /* up to the slot before the head, or the end of the ring */
        size_t end = ring->head > ring->tail ? ring->head - 1 :
                     ring->head ? ring->size : ring->size - 1;
        ssize_t r = iobuf_in(io, ring->buf + ring->tail, MIN(n - done, end - ring->tail));

        if (r < 0)
            return done ? (ssize_t) done : r;

        ring->tail = RING_INDEX(ring, ring->tail + r);
        done += r;
    }

    return done;
}

/** Writes `n` bytes into the ring, throwing away its oldest bytes if it
 * is full (the kernel's messages). Returns `n`. */
static inline size_t ringbuf_write_overwrite(struct ringbuf *ring, size_t n, char *buf)
{
    size_t size = n;

    while (n) {
        if (RING_INDEX(ring, ring->tail + 1) == ring->head) {
            /* the ring is full: drop the oldest byte */
            ring->head = RING_INDEX(ring, ring->head + 1);
        }

        ring->buf[ring->tail] = *buf++;
        ring->tail = RING_INDEX(ring, ring->tail + 1);
        n--;
    }

    return size - n;
}

#endif /* ! _DS_RINGBUF_H */
