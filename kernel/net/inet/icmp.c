/*
 * ICMP: echo requests (ping) are answered, echo replies go to the ping
 * socket whose identifier they have (its port), and "unreachable" to the
 * UDP socket whose datagram couldn't get there
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

#include <aquila/errno.h>

/* an "unreachable" for a datagram we sent: the socket that sent it */
static void icmp_unreachable_input(struct icmp_hdr *icmp, size_t len)
{
    /* the header of the datagram, and 8 bytes of it (UDP's header) */
    struct ipv4_hdr *ih = (struct ipv4_hdr *) (icmp + 1);

    if (len < sizeof(*icmp) + sizeof(*ih))
        return;

    size_t hlen = (ih->ver_ihl & 0xf) * 4;

    if (ih->proto != IP_PROTO_UDP || len < sizeof(*icmp) + hlen + sizeof(struct udp_hdr))
        return;

    struct udp_hdr *uh = (struct udp_hdr *) ((uint8_t *) ih + hlen);
    struct inet_sock *sock = inet_sock_find(IP_PROTO_UDP, ih->src, uh->src, ih->dst, uh->dst);

    if (sock)
        inet_sock_error(sock, icmp->code == ICMP_UNREACH_PORT ? ECONNREFUSED : EHOSTUNREACH);
}

void icmp_input(struct netif *netif, struct netbuf *nb, uint32_t src, uint32_t dst)
{
    struct icmp_hdr *icmp = (struct icmp_hdr *) nb->data;

    if (nb->len < sizeof(*icmp) || inet_checksum(nb->data, nb->len)) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    switch (icmp->type) {
        case ICMP_ECHO_REQUEST:
            /* the request turned into the reply, its data as it is (to
             * one of our addresses, not to a broadcast) */
            if (!ipv4_is_local(dst))
                break;

            icmp->type = ICMP_ECHO_REPLY;
            icmp->code = 0;
            icmp->checksum = 0;
            icmp->checksum = inet_checksum(nb->data, nb->len);

            /* from the address it was sent to */
            ipv4_output(netif, dst, src, IP_PROTO_ICMP, nb);
            return;

        case ICMP_ECHO_REPLY: {
            /* to the ping socket of that identifier, the ICMP header with
             * the data */
            struct inet_sock *sock = inet_sock_find(IP_PROTO_ICMP, dst, icmp->id, src, 0);

            if (!sock)
                break;

            nb->src_ip   = src;
            nb->src_port = 0;
            inet_sock_deliver(sock, nb);
            return;
        }

        case ICMP_UNREACH:
            icmp_unreachable_input(icmp, nb->len);
            break;
    }

    netbuf_free(nb);
}

/* the datagram of header `ih' (its payload follows it) can't get where it
 * was sent: its sender is told */
void icmp_unreachable(struct netif *netif, const struct ipv4_hdr *ih, int code)
{
    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return;

    /* the header, and 8 bytes of what followed it */
    size_t quote = (ih->ver_ihl & 0xf) * 4 + 8;

    if (quote > ntohs(ih->len))
        quote = ntohs(ih->len);

    struct icmp_hdr *icmp = netbuf_put(nb, sizeof(*icmp) + quote);

    icmp->type = ICMP_UNREACH;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->id   = 0;
    icmp->seq  = 0;
    memcpy(icmp + 1, ih, quote);
    icmp->checksum = inet_checksum(icmp, sizeof(*icmp) + quote);

    ipv4_output(netif, 0, ih->src, IP_PROTO_ICMP, nb);
}
