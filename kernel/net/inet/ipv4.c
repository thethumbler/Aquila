/** \file
 * IPv4.
 *
 * A packet for us goes to its protocol: ICMP, UDP or TCP. A packet to
 * send gets its IPv4 header here, and goes to its destination directly if
 * that is on the interface's link, or through the interface's gateway
 * otherwise. Fragments aren't supported either way: a fragment that
 * arrives is dropped, and what is sent is never fragmented.
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <net/tcp.h>

#include <aquila/errno.h>

static uint16_t ip_id;

static const uint8_t ether_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/** Returns whether `ip` is on `netif`'s link (its network). */
static int on_link(struct netif *netif, uint32_t ip)
{
    return (ip & netif->netmask) == (netif->ip & netif->netmask);
}

/** Returns whether `ip` is a broadcast address on `netif`'s link. */
static int is_broadcast(struct netif *netif, uint32_t ip)
{
    return ip == 0xffffffff || ip == (netif->ip | ~netif->netmask);
}

/** Returns whether `ip` is a loopback address (127.0.0.0/8). */
static int is_loopback(uint32_t ip)
{
    return (ntohl(ip) >> 24) == 127;
}

/**
 * Says whether an address is one of ours: a loopback address, or an
 * interface's.
 *
 * @param ip  the address, in network byte order
 * @return 1 if it is ours, 0 if not
 */
int ipv4_is_local(uint32_t ip)
{
    if (is_loopback(ip))
        return 1;

    for (struct netif *netif = netifs; netif; netif = netif->next)
        if (netif->ip && netif->ip == ip)
            return 1;

    return 0;
}

/**
 * Chooses the source address of a packet to `dst` sent through `netif`.
 * A packet to one of our own addresses (127.0.0.0/8 too) is sent from
 * that address; any other is sent from the interface's.
 *
 * @param netif  the interface it is sent through
 * @param dst    its destination
 * @return the source address
 */
uint32_t ipv4_source(struct netif *netif, uint32_t dst)
{
    return ipv4_is_local(dst) ? dst : netif->ip;
}

/**
 * Takes in an IPv4 packet that an interface received, and passes it to
 * its protocol. A packet that is malformed, not for us, or a fragment is
 * dropped; one for a protocol we don't have gets a "protocol unreachable".
 *
 * @param netif    the interface it came in on
 * @param nb       the packet, from its IPv4 header; it is freed or passed
 *                 on
 * @param src_mac  the sender's MAC address, to update ARP's cache
 */
void ipv4_input(struct netif *netif, struct netbuf *nb, const uint8_t src_mac[6])
{
    struct ipv4_hdr *ih = (struct ipv4_hdr *) nb->data;
    size_t hlen = (ih->ver_ihl & 0xf) * 4;

    if (nb->len < sizeof(*ih) || (ih->ver_ihl >> 4) != 4 || hlen < sizeof(*ih) ||
        hlen > nb->len || ntohs(ih->len) < hlen || ntohs(ih->len) > nb->len ||
        inet_checksum(ih, hlen)) {
        netif->stats.rx_errors++;
        goto drop;
    }

    /* keep it only if it is for us (the interface's address, another of
     * ours, or a broadcast), and not a fragment */
    if (ih->dst != netif->ip && !ipv4_is_local(ih->dst) && !is_broadcast(netif, ih->dst))
        goto drop;

    if (ntohs(ih->frag) & 0x3fff)   /* more fragments, or an offset */
        goto drop;

    /* learn the sender's hardware address, if it is on the link (lo has
     * none) */
    if (!(netif->flags & NETIF_LOOPBACK) && on_link(netif, ih->src))
        arp_update(netif, ih->src, src_mac);

    /* pass on the payload: take off the header (it stays in the buffer
     * before the payload, for ICMP errors) and the frame's padding */
    nb->len = ntohs(ih->len);
    netbuf_pull(nb, hlen);

    switch (ih->proto) {
        case IP_PROTO_ICMP:
            icmp_input(netif, nb, ih->src, ih->dst);
            return;
        case IP_PROTO_UDP:
            udp_input(netif, nb, ih);
            return;
        case IP_PROTO_TCP:
            tcp_input(netif, nb, ih);
            return;
    }

    /* no such protocol */
    if (!is_broadcast(netif, ih->dst))
        icmp_unreachable(netif, ih, 2);

drop:
    netif->stats.rx_dropped++;
    netbuf_free(nb);
}

/**
 * Chooses the interface to send a packet to `dst` through: the one whose
 * link it is on, or else the first one with a gateway.
 *
 * @param dst  the destination
 * @return the interface, or NULL if there is no route
 */
struct netif *ipv4_route(uint32_t dst)
{
    struct netif *gw = NULL;

    for (struct netif *netif = netifs; netif; netif = netif->next) {
        if (!(netif->flags & NETIF_UP))
            continue;

        /* lo takes 127.0.0.0/8, and nothing else */
        if (netif->flags & NETIF_LOOPBACK) {
            if (is_loopback(dst))
                return netif;
            continue;
        }

        /* a broadcast can go out of an interface that has no address yet
         * (a DHCP client's, from 0.0.0.0) */
        if (dst == 0xffffffff)
            return netif;

        if (!netif->ip)
            continue;

        /* on its link, or the interface's own address */
        if (on_link(netif, dst) || dst == netif->ip)
            return netif;

        if (!gw && netif->gateway)
            gw = netif;
    }

    return gw;
}

/**
 * Sends a packet through an interface: adds its IPv4 header and sends it
 * to `dst`, or to the gateway if `dst` isn't on the link, once ARP knows
 * the hardware address. A packet to one of our own addresses loops back.
 * The netbuf is freed (or kept by ARP until it can be sent).
 *
 * @param netif  the interface
 * @param src    the source address, or 0 for `ipv4_source()`'s
 * @param dst    the destination
 * @param proto  the protocol (`IP_PROTO_*`)
 * @param nb     the payload, with room for the headers before it
 * @return 0, `-ENETUNREACH` if there is no gateway to send it through, or
 *         a negative errno from the interface
 */
int ipv4_output(struct netif *netif, uint32_t src, uint32_t dst, uint8_t proto, struct netbuf *nb)
{
    struct ipv4_hdr *ih = netbuf_push(nb, sizeof(*ih));

    ih->ver_ihl  = 0x45;
    ih->tos      = 0;
    ih->len      = htons(nb->len);
    ih->id       = htons(ip_id++);
    ih->frag     = htons(0x4000);   /* don't fragment */
    ih->ttl      = 64;
    ih->proto    = proto;
    ih->checksum = 0;
    ih->src      = src ? src : ipv4_source(netif, dst);
    ih->dst      = dst;
    ih->checksum = inet_checksum(ih, sizeof(*ih));

    /* a packet to ourselves comes back in without going on the wire. It
     * comes in from the network's thread, not from here: otherwise TCP's
     * answer would come back into the connection while it is sending */
    if (ipv4_is_local(dst)) {
        netif->stats.tx_packets++;
        netif_loopback(netif, nb);
        return 0;
    }

    if (is_broadcast(netif, dst))
        return ether_output(netif, ether_broadcast, ETHER_TYPE_IPV4, nb);

    uint32_t hop = on_link(netif, dst) ? dst : netif->gateway;
    uint8_t mac[6];

    if (!hop) {
        netif->stats.tx_dropped++;
        netbuf_free(nb);
        return -ENETUNREACH;
    }

    /* if the hardware address isn't known yet, the packet waits for ARP's
     * answer */
    if (arp_lookup(netif, hop, mac))
        return arp_resolve(netif, hop, nb);

    return ether_output(netif, mac, ETHER_TYPE_IPV4, nb);
}

/**
 * Sends a packet to `dst`, through the interface its route says. The
 * netbuf is freed.
 *
 * @param dst    the destination
 * @param proto  the protocol (`IP_PROTO_*`)
 * @param nb     the payload, with room for the headers before it
 * @return 0, `-ENETUNREACH` if there is no route, or a negative errno
 */
int ipv4_send(uint32_t dst, uint8_t proto, struct netbuf *nb)
{
    struct netif *netif = ipv4_route(dst);

    if (!netif) {
        netbuf_free(nb);
        return -ENETUNREACH;
    }

    return ipv4_output(netif, 0, dst, proto, nb);
}
