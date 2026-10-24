#ifndef _NET_INET_H
#define _NET_INET_H

/*
 * The Internet protocols over Ethernet (kernel/net/inet): their headers,
 * in the network's byte order, and the functions each layer calls on the
 * next one. See docs/kernel/networking.md.
 */

#include <core/system.h>
#include <net/netbuf.h>
#include <net/netif.h>

#include <aquila/socket.h>

/* What an Ethernet frame carries. */
#define ETHER_TYPE_IPV4     0x0800
#define ETHER_TYPE_ARP      0x0806

/** An Ethernet frame's header. */
struct ether_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __packed;

/** An ARP packet, for IPv4 over Ethernet. */
struct arp_hdr {
    uint16_t htype;     /* 1, for Ethernet */
    uint16_t ptype;     /* IPv4 */
    uint8_t  hlen;      /* 6 */
    uint8_t  plen;      /* 4 */
    uint16_t op;        /* 1 for a request, 2 for a reply */
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __packed;

/* What an IPv4 packet carries. */
#define IP_PROTO_ICMP       1
#define IP_PROTO_TCP        6
#define IP_PROTO_UDP        17

/** An IPv4 packet's header. */
struct ipv4_hdr {
    uint8_t  ver_ihl;   /* the version (4) and the header's length (in 32-bit words) */
    uint8_t  tos;
    uint16_t len;       /* the whole packet's length */
    uint16_t id;
    uint16_t frag;      /* flags and fragment offset */
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __packed;

/* ICMP messages' types, and the codes of "unreachable" messages. */
#define ICMP_ECHO_REPLY     0
#define ICMP_UNREACH        3
#define ICMP_ECHO_REQUEST   8

#define ICMP_UNREACH_HOST   1
#define ICMP_UNREACH_PORT   3

/** An ICMP message's header. */
struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __packed;

/** A UDP datagram's header. */
struct udp_hdr {
    uint16_t src;
    uint16_t dst;
    uint16_t len;       /* the length of the header and the data */
    uint16_t checksum;  /* 0 means none */
} __packed;

/* Returns the Internet checksum of `len` bytes. */
uint16_t inet_checksum(const void *data, size_t len);

/* Returns the Internet checksum of the pseudo-header (src, dst, proto,
 * len) and the data, as UDP and TCP use. */
uint16_t inet_checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto, const void *data, size_t len);

struct inet_sock;

/* Each layer's input (a packet that came) and output (one to send). */
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

/* The sockets (net/inet/socket.c): finding the one a packet is for,
 * giving it the packet or an error, and their addresses. */
struct inet_sock *inet_sock_find(struct netif *netif, uint8_t proto, uint32_t local, uint16_t port,
                                 uint32_t remote, uint16_t remote_port);
void inet_sock_deliver(struct inet_sock *sock, struct netbuf *nb);
void inet_sock_error(struct inet_sock *sock, int err);
int  inet_sockaddr_get(const struct sockaddr *addr, socklen_t len, uint32_t *ip, uint16_t *port);
void inet_sockaddr_put(struct sockaddr *addr, socklen_t *len, uint32_t ip, uint16_t port);

/* The interfaces' ioctl() requests (net/inet/ifioctl.c). */
int inet_ifioctl(int request, void *argp);

#endif /* ! _NET_INET_H */
