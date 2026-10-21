/*
 * IPv4: packets for us to their protocols, and the header of what is
 * sent (no fragments either way, no routing but the gateway)
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

static uint16_t ip_id;

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

    /* for us (our address, or a broadcast), not a fragment */
    uint32_t bcast = netif->ip | ~netif->netmask;

    if (ih->dst != netif->ip && ih->dst != bcast && ih->dst != 0xffffffff)
        goto drop;

    if (ntohs(ih->frag) & 0x3fff)   /* more fragments, or an offset */
        goto drop;

    uint32_t src = ih->src, dst = ih->dst;
    uint8_t proto = ih->proto;

    /* the sender's hardware address, if it is on the link */
    if ((src & netif->netmask) == (netif->ip & netif->netmask))
        arp_update(netif, src, src_mac);

    /* the payload: the header off, the frame's padding too */
    nb->len = ntohs(ih->len);
    netbuf_pull(nb, hlen);

    switch (proto) {
        case IP_PROTO_ICMP:
            icmp_input(netif, nb, src, dst);
            return;
    }

drop:
    netif->stats.rx_dropped++;
    netbuf_free(nb);
}

/* the packet goes to `dst' (through the gateway if it isn't on the link,
 * if its hardware address is known), the netbuf is freed */
int ipv4_output(struct netif *netif, uint32_t dst, uint8_t proto, struct netbuf *nb)
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
    ih->src      = netif->ip;
    ih->dst      = dst;
    ih->checksum = inet_checksum(ih, sizeof(*ih));

    uint32_t hop = (dst & netif->netmask) == (netif->ip & netif->netmask) ? dst : netif->gateway;
    uint8_t mac[6];

    if (arp_lookup(netif, hop, mac)) {
        /* resolving comes with sending of our own (UDP) */
        netif->stats.tx_dropped++;
        netbuf_free(nb);
        return -1;
    }

    return ether_output(netif, mac, ETHER_TYPE_IPV4, nb);
}
