#ifndef _NET_NETIF_H
#define _NET_NETIF_H

#include <core/system.h>
#include <net/netbuf.h>

#define NETIF_UP        0x0001  /* configured, it takes packets */
#define NETIF_RUNNING   0x0002  /* the link is up */

/**
 * \ingroup net
 * \brief a network interface: a device that sends and receives Ethernet
 * frames, and its IPv4 configuration
 */
struct netif {
    char name[8];               /* eth0, eth1, ... */
    uint8_t mac[6];
    uint16_t mtu;
    int flags;

    /* IPv4, in the network's byte order, 0 if not configured */
    uint32_t ip, netmask, gateway;

    /* send a frame (its Ethernet header included), the netbuf stays the
     * caller's */
    int (*transmit)(struct netif *netif, struct netbuf *nb);

    void *priv;                 /* the driver's */

    struct {
        uint64_t rx_packets, rx_bytes, rx_errors, rx_dropped;
        uint64_t tx_packets, tx_bytes, tx_errors, tx_dropped;
    } stats;

    struct netif *next;
};

extern struct netif *netifs;

int  netif_register(struct netif *netif);
void netif_input(struct netif *netif, struct netbuf *nb);
int  netif_output(struct netif *netif, struct netbuf *nb);

#endif /* ! _NET_NETIF_H */
