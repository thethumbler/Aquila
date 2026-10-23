#ifndef _NET_NETBUF_H
#define _NET_NETBUF_H

#include <core/system.h>
#include <core/panic.h>

struct netif;

/**
 * a packet: one buffer of NETBUF_SIZE bytes, the header at its
 * start, the packet a window in the rest (data, len); each layer takes
 * its header off the front going up, and puts it in the room before the
 * data going down, without copying
 */
struct netbuf {
    struct netbuf *next;    /* in a queue, or the free list */
    struct netif *netif;    /* received on */
    uint8_t *data;
    size_t len;

    int loopback;           /* sent to ourselves: an IPv4 packet */

    /* where a datagram came from (in a socket's queue) */
    uint32_t src_ip;
    uint16_t src_port;

    /* a TCP segment that came before its turn: where its data is, and if
     * a FIN follows it */
    uint32_t seq;
    int fin;

    /* its page of memory; the one before it on the free list (net/netbuf.c) */
    paddr_t page;
    struct netbuf *free_prev;
};

#define NETBUF_SIZE     2048
#define NETBUF_HEADROOM 128     /* for the headers going down */

/* the end of its room */
#define NETBUF_END(nb)  ((uint8_t *) (nb) + NETBUF_SIZE)

struct netbuf *netbuf_alloc(void);
void netbuf_free(struct netbuf *nb);

/** room for a header of `size' bytes before the data */
static inline void *netbuf_push(struct netbuf *nb, size_t size)
{
    if (nb->data - size < (uint8_t *) (nb + 1))
        panic("netbuf: no room for a header");

    nb->data -= size;
    nb->len  += size;
    return nb->data;
}

/** the header of `size' bytes at the front off, NULL if it isn't there */
static inline void *netbuf_pull(struct netbuf *nb, size_t size)
{
    if (nb->len < size)
        return NULL;

    void *header = nb->data;
    nb->data += size;
    nb->len  -= size;
    return header;
}

/** `size' more bytes at the end, NULL if there is no room */
static inline void *netbuf_put(struct netbuf *nb, size_t size)
{
    if (nb->data + nb->len + size > NETBUF_END(nb))
        return NULL;

    void *tail = nb->data + nb->len;
    nb->len += size;
    return tail;
}

#endif /* ! _NET_NETBUF_H */
