/*
 * IPv4: packets for us to their protocols, and the header of what is
 * sent (no fragments either way); what isn't on an interface's link goes
 * through its gateway
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

#include <aquila/errno.h>

static uint16_t ip_id;

static const uint8_t ether_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static int on_link(struct netif *netif, uint32_t ip)
{
    return (ip & netif->netmask) == (netif->ip & netif->netmask);
}

static int is_broadcast(struct netif *netif, uint32_t ip)
{
    return ip == 0xffffffff || ip == (netif->ip | ~netif->netmask);
}

static int is_loopback(uint32_t ip)
{
    return (ntohl(ip) >> 24) == 127;
}

/* ours: loopback, or an interface's */
int ipv4_is_local(uint32_t ip)
{
    if (is_loopback(ip))
        return 1;

    for (struct netif *netif = netifs; netif; netif = netif->next)
        if (netif->ip && netif->ip == ip)
            return 1;

    return 0;
}

/* the address a packet to `dst' through `netif' is from: loopback's is
 * the one it's sent to */
uint32_t ipv4_source(struct netif *netif, uint32_t dst)
{
    return is_loopback(dst) ? dst : netif->ip;
}

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

    /* for us (our address, ours sent to ourselves, or a broadcast), not a
     * fragment */
    if (ih->dst != netif->ip && !ipv4_is_local(ih->dst) && !is_broadcast(netif, ih->dst))
        goto drop;

    if (ntohs(ih->frag) & 0x3fff)   /* more fragments, or an offset */
        goto drop;

    /* the sender's hardware address, if it is on the link */
    if (on_link(netif, ih->src))
        arp_update(netif, ih->src, src_mac);

    /* the payload: the header off (it stays before it, for ICMP errors),
     * the frame's padding too */
    nb->len = ntohs(ih->len);
    netbuf_pull(nb, hlen);

    switch (ih->proto) {
        case IP_PROTO_ICMP:
            icmp_input(netif, nb, ih->src, ih->dst);
            return;
        case IP_PROTO_UDP:
            udp_input(netif, nb, ih);
            return;
    }

    /* no such protocol */
    if (!is_broadcast(netif, ih->dst))
        icmp_unreachable(netif, ih, 2);

drop:
    netif->stats.rx_dropped++;
    netbuf_free(nb);
}

/* the interface to `dst': the one whose link it is on, else the first
 * with a gateway; NULL if none */
struct netif *ipv4_route(uint32_t dst)
{
    struct netif *gw = NULL;

    for (struct netif *netif = netifs; netif; netif = netif->next) {
        if (!(netif->flags & NETIF_UP))
            continue;

        /* a broadcast goes from an interface without an address too (a
         * DHCP client's, from 0.0.0.0) */
        if (dst == 0xffffffff)
            return netif;

        if (!netif->ip)
            continue;

        if (on_link(netif, dst) || dst == 0xffffffff || ipv4_is_local(dst))
            return netif;

        if (!gw && netif->gateway)
            gw = netif;
    }

    return gw;
}

/* the packet goes from `src' (0: ipv4_source()) to `dst' through `netif'
 * (through its gateway if it isn't on the link), once its hardware
 * address is known; the netbuf is freed */
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

    /* to ourselves: in again, without the wire */
    if (ipv4_is_local(dst)) {
        netif->stats.tx_packets++;
        ipv4_input(netif, nb, netif->mac);
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

    /* it waits for ARP's answer */
    if (arp_lookup(netif, hop, mac))
        return arp_resolve(netif, hop, nb);

    return ether_output(netif, mac, ETHER_TYPE_IPV4, nb);
}

/* the packet goes to `dst', on its route; the netbuf is freed */
int ipv4_send(uint32_t dst, uint8_t proto, struct netbuf *nb)
{
    struct netif *netif = ipv4_route(dst);

    if (!netif) {
        netbuf_free(nb);
        return -ENETUNREACH;
    }

    return ipv4_output(netif, 0, dst, proto, nb);
}
