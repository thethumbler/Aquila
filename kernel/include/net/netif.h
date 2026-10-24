#ifndef _NET_NETIF_H
#define _NET_NETIF_H

#include <core/system.h>
#include <net/netbuf.h>

/* A network interface's flags. */
#define NETIF_UP        0x0001  /* it's configured, and takes packets */
#define NETIF_RUNNING   0x0002  /* its link is up */
#define NETIF_LOOPBACK  0x0004  /* it's lo, 127/8: what it sends comes back */

/**
 * A network interface (net/netif.c): a device that sends and receives
 * Ethernet frames, and its IPv4 configuration.
 */
struct netif {
    char name[8];               /* lo, eth0, eth1... (the driver's, or ethN) */
    uint8_t mac[6];
    uint16_t mtu;
    int flags;

    /* its IPv4 configuration, in the network's byte order (0 if not
     * configured) */
    uint32_t ip, netmask, gateway;

    /* sends a frame (its Ethernet header included); the netbuf stays the
     * caller's */
    int (*transmit)(struct netif *netif, struct netbuf *nb);

    void *priv;                 /* the driver's own data */

    struct {
        uint64_t rx_packets, rx_bytes, rx_errors, rx_dropped;
        uint64_t tx_packets, tx_bytes, tx_errors, tx_dropped;
    } stats;

    struct netif *next;
};

/* The interfaces. */
extern struct netif *netifs;

/* Registers an interface; finds one; a frame that came in on one; sends a
 * frame, or loops it back. */
int  netif_register(struct netif *netif);
struct netif *netif_find(const char *name);
void netif_input(struct netif *netif, struct netbuf *nb);
void netif_loopback(struct netif *netif, struct netbuf *nb);
int  netif_output(struct netif *netif, struct netbuf *nb);

/* Calls fn(arg) in `ns` nanoseconds, from the network's thread. */
struct callout;
void net_callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg);

#endif /* ! _NET_NETIF_H */
