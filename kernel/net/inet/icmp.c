/** \file
 * ICMP.
 *
 * An echo request (a ping) is answered. An echo reply goes to the ping
 * socket whose identifier it carries (the socket's port). A "destination
 * unreachable" goes to the UDP socket whose datagram couldn't get there,
 * as an error. This file also sends "unreachable" for the datagrams that
 * arrive for no socket.
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

#include <aquila/errno.h>

/** Handles a "destination unreachable" about a datagram we sent: the UDP
 * socket that sent it gets the error. */
static void icmp_unreachable_input(struct netif *netif, struct icmp_hdr *icmp, size_t len)
{
    /* it quotes the datagram's IPv4 header and its first 8 bytes (the UDP
     * header) */
    struct ipv4_hdr *ih = (struct ipv4_hdr *) (icmp + 1);

    if (len < sizeof(*icmp) + sizeof(*ih))
        return;

    size_t hlen = (ih->ver_ihl & 0xf) * 4;

    if (ih->proto != IP_PROTO_UDP || len < sizeof(*icmp) + hlen + sizeof(struct udp_hdr))
        return;

    struct udp_hdr *uh = (struct udp_hdr *) ((uint8_t *) ih + hlen);
    struct inet_sock *sock = inet_sock_find(netif, IP_PROTO_UDP, ih->src, uh->src, ih->dst, uh->dst);

    if (sock)
        inet_sock_error(sock, icmp->code == ICMP_UNREACH_PORT ? ECONNREFUSED : EHOSTUNREACH);
}

/**
 * Takes in an ICMP message that IPv4 received.
 *
 * @param netif  the interface it came in on
 * @param nb     the message, from its ICMP header; it is freed, sent back
 *               or delivered
 * @param src    the sender's address
 * @param dst    the address it was sent to
 */
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
            /* turn the request into the reply, with the same data (only for
             * a request sent to one of our addresses, not a broadcast) */
            if (!ipv4_is_local(dst))
                break;

            icmp->type = ICMP_ECHO_REPLY;
            icmp->code = 0;
            icmp->checksum = 0;
            icmp->checksum = inet_checksum(nb->data, nb->len);

            /* reply from the address it was sent to */
            ipv4_output(netif, dst, src, IP_PROTO_ICMP, nb);
            return;

        case ICMP_ECHO_REPLY: {
            /* deliver it, ICMP header and data, to the ping socket with
             * that identifier */
            struct inet_sock *sock = inet_sock_find(netif, IP_PROTO_ICMP, dst, icmp->id, src, 0);

            if (!sock)
                break;

            nb->src_ip   = src;
            nb->src_port = 0;
            inet_sock_deliver(sock, nb);
            return;
        }

        case ICMP_UNREACH:
            icmp_unreachable_input(netif, icmp, nb->len);
            break;
    }

    netbuf_free(nb);
}

/**
 * Tells the sender of a datagram that it couldn't get where it was sent.
 *
 * @param netif  the interface the datagram came in on
 * @param ih     the datagram's IPv4 header, with its payload after it
 * @param code   why (`ICMP_UNREACH_*`)
 */
void icmp_unreachable(struct netif *netif, const struct ipv4_hdr *ih, int code)
{
    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return;

    /* quote the header, and the first 8 bytes after it */
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
