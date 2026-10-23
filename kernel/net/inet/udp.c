/** \file
 * UDP: datagrams to the sockets bound to their port, and "port
 * unreachable" if there is none
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

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
        /* to one of our addresses, not to a broadcast */
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
