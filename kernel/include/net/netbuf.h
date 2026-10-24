#ifndef _NET_NETBUF_H
#define _NET_NETBUF_H

#include <core/system.h>
#include <core/panic.h>

struct netif;

/**
 * A packet (net/netbuf.c). It is one buffer of NETBUF_SIZE bytes: this
 * header at its start, and the packet a window in the rest (`data`,
 * `len`). Going up, each layer takes its header off the front; going
 * down, it puts its header in the room before the data. Nothing is
 * copied.
 */
struct netbuf {
    struct netbuf *next;    /* the next one in a queue, or on the free list */
    struct netif *netif;    /* the interface it came in on */
    uint8_t *data;
    size_t len;

    int loopback;           /* whether it's an IPv4 packet sent to ourselves */

    /* where a datagram came from (in a socket's queue) */
    uint32_t src_ip;
    uint16_t src_port;

    /* for a TCP segment that came before its turn: where its data goes,
     * and whether a FIN follows it */
    uint32_t seq;
    int fin;

    /* its page of memory, and the netbuf before it on the free list */
    paddr_t page;
    struct netbuf *free_prev;
};

/* A netbuf's size, and the room kept before the data for the headers
 * that the layers below add. */
#define NETBUF_SIZE     2048
#define NETBUF_HEADROOM 128

/* The end of a netbuf's room. */
#define NETBUF_END(nb)  ((uint8_t *) (nb) + NETBUF_SIZE)

/* Allocates a netbuf, or frees one. */
struct netbuf *netbuf_alloc(void);
void netbuf_free(struct netbuf *nb);

/** Makes room for a header of `size` bytes before the data, and returns
 * it. Panics if there's no room: the headroom is always enough. */
static inline void *netbuf_push(struct netbuf *nb, size_t size)
{
    if (nb->data - size < (uint8_t *) (nb + 1))
        panic("netbuf: no room for a header");

    nb->data -= size;
    nb->len  += size;
    return nb->data;
}

/** Takes the header of `size` bytes off the front of the packet, and
 * returns it, or NULL if the packet is shorter. */
static inline void *netbuf_pull(struct netbuf *nb, size_t size)
{
    if (nb->len < size)
        return NULL;

    void *header = nb->data;
    nb->data += size;
    nb->len  -= size;
    return header;
}

/** Adds `size` bytes at the end of the packet, and returns them, or NULL
 * if there's no room. */
static inline void *netbuf_put(struct netbuf *nb, size_t size)
{
    if (nb->data + nb->len + size > NETBUF_END(nb))
        return NULL;

    void *tail = nb->data + nb->len;
    nb->len += size;
    return tail;
}

#endif /* ! _NET_NETBUF_H */
