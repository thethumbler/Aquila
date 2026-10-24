/** \file
 * UDP input.
 *
 * A UDP datagram that arrives goes to the socket bound to its destination
 * port. If no socket is bound there, the sender gets an ICMP "port
 * unreachable" (unless the datagram was a broadcast), and the datagram is
 * dropped. Sending is the sockets' (`net/inet/socket.c`).
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

/**
 * Takes in a UDP datagram that IPv4 received, and delivers it to its
 * socket. A datagram with a bad length or checksum is dropped.
 *
 * @param netif  the interface it came in on
 * @param nb     the datagram, from its UDP header; it is freed or given
 *               to the socket
 * @param ih     the IPv4 header it came with
 */
void udp_input(struct netif *netif, struct netbuf *nb, const struct ipv4_hdr *ih)
{
    struct udp_hdr *uh = (struct udp_hdr *) nb->data;
    size_t len = nb->len;

    if (len < sizeof(*uh) || ntohs(uh->len) < sizeof(*uh) || ntohs(uh->len) > len ||
        (uh->checksum && inet_checksum_pseudo(ih->src, ih->dst, IP_PROTO_UDP, uh, ntohs(uh->len)))) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    nb->len = ntohs(uh->len);

    struct inet_sock *sock = inet_sock_find(netif, IP_PROTO_UDP, ih->dst, uh->dst, ih->src, uh->src);

    if (!sock) {
        /* only for a datagram sent to one of our addresses, not a broadcast */
        if (ipv4_is_local(ih->dst))
            icmp_unreachable(netif, ih, ICMP_UNREACH_PORT);

        netif->stats.rx_dropped++;
        netbuf_free(nb);
        return;
    }

    nb->src_ip   = ih->src;
    nb->src_port = uh->src;
    netbuf_pull(nb, sizeof(*uh));

    inet_sock_deliver(sock, nb);
}
