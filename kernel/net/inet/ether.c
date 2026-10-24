/** \file
 * Ethernet, and the Internet checksum.
 *
 * A frame that arrives goes to the protocol its type says (ARP or IPv4);
 * a frame to send gets its Ethernet header here. The Internet checksum,
 * which IPv4, ICMP, UDP and TCP use, is here too.
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

/**
 * Takes in a frame that an interface received, and passes it to ARP or
 * IPv4. A frame of another type, or too short, is dropped.
 *
 * @param netif  the interface it came in on
 * @param nb     the frame; it is freed or passed on
 */
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

/**
 * Sends a frame: adds its Ethernet header and gives it to the interface.
 * The netbuf is freed in every case.
 *
 * @param netif  the interface to send it on
 * @param dst    the destination MAC address
 * @param type   the frame's type (`ETHER_TYPE_*`)
 * @param nb     the frame's data, with room for the header before it
 * @return 0, or a negative errno from the interface
 */
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

/**
 * Computes the Internet checksum (RFC 1071): the ones' complement of the
 * ones' complement sum of the data's 16-bit words.
 *
 * @param data  the data
 * @param len   its length, in bytes
 * @return the checksum, in network byte order (0 if the data includes a
 *         correct checksum)
 */
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

/**
 * Computes the Internet checksum of data with the IPv4 pseudo-header
 * before it (the addresses, the protocol and the length), as UDP and TCP
 * do.
 *
 * @param src    the source address
 * @param dst    the destination address
 * @param proto  the protocol (`IP_PROTO_*`)
 * @param data   the UDP or TCP header and data
 * @param len    their length, in bytes
 * @return the checksum, in network byte order (0 if the data includes a
 *         correct checksum)
 */
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
