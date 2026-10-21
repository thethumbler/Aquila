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
#define ICMP_ECHO_REQUEST   8

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __packed;

/* the Internet checksum of `len' bytes */
uint16_t inet_checksum(const void *data, size_t len);

void ether_input(struct netif *netif, struct netbuf *nb);
int  ether_output(struct netif *netif, const uint8_t dst[6], uint16_t type, struct netbuf *nb);

void arp_input(struct netif *netif, struct netbuf *nb);
void arp_update(struct netif *netif, uint32_t ip, const uint8_t mac[6]);
int  arp_lookup(struct netif *netif, uint32_t ip, uint8_t mac[6]);

void ipv4_input(struct netif *netif, struct netbuf *nb, const uint8_t src_mac[6]);
int  ipv4_output(struct netif *netif, uint32_t dst, uint8_t proto, struct netbuf *nb);

void icmp_input(struct netif *netif, struct netbuf *nb, uint32_t src, uint32_t dst);

#endif /* ! _NET_INET_H */
