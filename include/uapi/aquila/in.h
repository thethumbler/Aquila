#ifndef _AQUILA_IN_H
#define _AQUILA_IN_H

/* Internet (IPv4) addresses, shared by the kernel and <netinet/in.h> */

#include <stdint.h>
#include <aquila/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;               /* the network's byte order */
};

/* 20 bytes: sa_family_t is 32 bits here */
struct sockaddr_in {
    sa_family_t    sin_family;      /* AF_INET */
    in_port_t      sin_port;        /* the network's byte order */
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_IPV6    41
#define IPPROTO_RAW     255

#define INADDR_ANY          ((in_addr_t) 0x00000000)
#define INADDR_BROADCAST    ((in_addr_t) 0xffffffff)
#define INADDR_NONE         ((in_addr_t) 0xffffffff)
#define INADDR_LOOPBACK     ((in_addr_t) 0x7f000001)

#endif /* ! _AQUILA_IN_H */
