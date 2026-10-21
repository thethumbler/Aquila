/*
 * ICMP: echo requests (ping) are answered
 */

#include <core/system.h>
#include <net/byteorder.h>
#include <net/inet.h>

void icmp_input(struct netif *netif, struct netbuf *nb, uint32_t src, uint32_t dst)
{
    struct icmp_hdr *icmp = (struct icmp_hdr *) nb->data;

    if (nb->len < sizeof(*icmp) || inet_checksum(nb->data, nb->len)) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    /* the request turned into the reply, its data as it is (not to a
     * broadcast) */
    if (icmp->type == ICMP_ECHO_REQUEST && dst == netif->ip) {
        icmp->type = ICMP_ECHO_REPLY;
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->checksum = inet_checksum(nb->data, nb->len);

        ipv4_output(netif, src, IP_PROTO_ICMP, nb);
        return;
    }

    netbuf_free(nb);
}
