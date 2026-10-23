/** \file
 * Ethernet: frames to the protocols by their type, and the header of
 * what is sent
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

void ether_input(struct netif *netif, struct netbuf *nb)
{
    struct ether_hdr *eh = netbuf_pull(nb, sizeof(*eh));

    if (!eh) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    switch (ntohs(eh->type)) {
        case ETHER_TYPE_ARP:
            arp_input(netif, nb);
            return;
        case ETHER_TYPE_IPV4:
            ipv4_input(netif, nb, eh->src);
            return;
    }

    netif->stats.rx_dropped++;
    netbuf_free(nb);
}

/** the frame goes, the netbuf is freed */
int ether_output(struct netif *netif, const uint8_t dst[6], uint16_t type, struct netbuf *nb)
{
    struct ether_hdr *eh = netbuf_push(nb, sizeof(*eh));

    memcpy(eh->dst, dst, 6);
    memcpy(eh->src, netif->mac, 6);
    eh->type = htons(type);

    int err = netif_output(netif, nb);
    netbuf_free(nb);
    return err;
}

/** the Internet checksum (RFC 1071): the ones' complement of the ones'
 * complement sum of the 16-bit words */
uint16_t inet_checksum(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t sum = 0;

    for (; len > 1; len -= 2, p += 2)
        sum += (uint32_t) p[0] << 8 | p[1];

    if (len)
        sum += (uint32_t) p[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return htons(~sum & 0xffff);
}

/** over the pseudo-header too: the addresses, the protocol and the length
 * (UDP's and TCP's checksums) */
uint16_t inet_checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto, const void *data, size_t len)
{
    struct {
        uint32_t src, dst;
        uint8_t zero, proto;
        uint16_t len;
    } __packed pseudo = {src, dst, 0, proto, htons(len)};

    const uint8_t *p = (const uint8_t *) &pseudo;
    uint32_t sum = 0;

    for (size_t i = 0; i < sizeof(pseudo); i += 2)
        sum += (uint32_t) p[i] << 8 | p[i + 1];

    for (p = data; len > 1; len -= 2, p += 2)
        sum += (uint32_t) p[0] << 8 | p[1];

    if (len)
        sum += (uint32_t) p[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return htons(~sum & 0xffff);
}
