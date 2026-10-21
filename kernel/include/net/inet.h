#ifndef _NET_INET_H
#define _NET_INET_H

/*
 * The Internet protocols over Ethernet (kernel/net/inet): their headers,
 * in the network's byte order, and how the layers call each other
 */

#include <core/system.h>
#include <net/netbuf.h>
#include <net/netif.h>

#define ETHER_TYPE_IPV4     0x0800
#define ETHER_TYPE_ARP      0x0806

struct ether_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __packed;

struct arp_hdr {
    uint16_t htype;     /* 1, Ethernet */
    uint16_t ptype;     /* IPv4 */
    uint8_t  hlen;      /* 6 */
    uint8_t  plen;      /* 4 */
    uint16_t op;        /* request 1, reply 2 */
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __packed;

#define IP_PROTO_ICMP       1
#define IP_PROTO_UDP        17

struct ipv4_hdr {
    uint8_t  ver_ihl;   /* version (4) and header length (in 32-bit words) */
    uint8_t  tos;
    uint16_t len;       /* of the whole packet */
    uint16_t id;
    uint16_t frag;      /* flags and fragment offset */
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __packed;

#define ICMP_ECHO_REPLY     0
#define ICMP_UNREACH        3
#define ICMP_ECHO_REQUEST   8

#define ICMP_UNREACH_HOST   1
#define ICMP_UNREACH_PORT   3

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __packed;

struct udp_hdr {
    uint16_t src;
    uint16_t dst;
    uint16_t len;       /* of the header and the data */
    uint16_t checksum;  /* 0: none */
} __packed;

/* the Internet checksum of `len' bytes */
uint16_t inet_checksum(const void *data, size_t len);

/* the same, of the pseudo-header (src, dst, proto, len) and the data */
uint16_t inet_checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto, const void *data, size_t len);

struct inet_sock;

void ether_input(struct netif *netif, struct netbuf *nb);
int  ether_output(struct netif *netif, const uint8_t dst[6], uint16_t type, struct netbuf *nb);

void arp_input(struct netif *netif, struct netbuf *nb);
void arp_update(struct netif *netif, uint32_t ip, const uint8_t mac[6]);
int  arp_lookup(struct netif *netif, uint32_t ip, uint8_t mac[6]);
int  arp_resolve(struct netif *netif, uint32_t ip, struct netbuf *nb);

void ipv4_input(struct netif *netif, struct netbuf *nb, const uint8_t src_mac[6]);
int  ipv4_output(struct netif *netif, uint32_t src, uint32_t dst, uint8_t proto, struct netbuf *nb);
uint32_t ipv4_source(struct netif *netif, uint32_t dst);
int  ipv4_send(uint32_t dst, uint8_t proto, struct netbuf *nb);
struct netif *ipv4_route(uint32_t dst);
int  ipv4_is_local(uint32_t ip);

void icmp_input(struct netif *netif, struct netbuf *nb, uint32_t src, uint32_t dst);
void icmp_unreachable(struct netif *netif, const struct ipv4_hdr *ih, int code);

void udp_input(struct netif *netif, struct netbuf *nb, const struct ipv4_hdr *ih);

/* the sockets (net/inet/socket.c) */
struct inet_sock *inet_sock_find(uint8_t proto, uint32_t local, uint16_t port,
                                 uint32_t remote, uint16_t remote_port);
void inet_sock_deliver(struct inet_sock *sock, struct netbuf *nb);
void inet_sock_error(struct inet_sock *sock, int err);

/* the interfaces' requests (net/inet/ifioctl.c) */
int inet_ifioctl(int request, void *argp);

#endif /* ! _NET_INET_H */
