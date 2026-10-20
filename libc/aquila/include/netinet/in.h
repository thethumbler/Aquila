#ifndef _NETINET_IN_H
#define _NETINET_IN_H

/*
 * Internet addresses. The kernel has no network stack yet (only AF_UNIX
 * sockets): these are for programs that know of them.
 */

#include <stdint.h>
#include <sys/socket.h>

/* i386 is little-endian, network byte order is big-endian */
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t    sin_family;  /* AF_INET */
    in_port_t      sin_port;    /* network byte order */
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};

struct in6_addr {
    uint8_t s6_addr[16];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;    /* AF_INET6 */
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

extern const struct in6_addr in6addr_any, in6addr_loopback;

/* what kind of IPv6 address */
#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (((const uint32_t *) (a)->s6_addr)[0] == 0 && ((const uint32_t *) (a)->s6_addr)[1] == 0 && \
     ((const uint32_t *) (a)->s6_addr)[2] == 0 && ((const uint32_t *) (a)->s6_addr)[3] == 0)
#define IN6_IS_ADDR_LOOPBACK(a) \
    (((const uint32_t *) (a)->s6_addr)[0] == 0 && ((const uint32_t *) (a)->s6_addr)[1] == 0 && \
     ((const uint32_t *) (a)->s6_addr)[2] == 0 && ((const uint32_t *) (a)->s6_addr)[3] == htonl(1))
#define IN6_IS_ADDR_V4MAPPED(a) \
    (((const uint32_t *) (a)->s6_addr)[0] == 0 && ((const uint32_t *) (a)->s6_addr)[1] == 0 && \
     ((const uint32_t *) (a)->s6_addr)[2] == htonl(0xffff))
#define IN6_IS_ADDR_V4COMPAT(a) \
    (((const uint32_t *) (a)->s6_addr)[0] == 0 && ((const uint32_t *) (a)->s6_addr)[1] == 0 && \
     ((const uint32_t *) (a)->s6_addr)[2] == 0 && ntohl(((const uint32_t *) (a)->s6_addr)[3]) > 1)
#define IN6_IS_ADDR_MULTICAST(a)    ((a)->s6_addr[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a)    ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_SITELOCAL(a)    ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0xc0)
#define IN6ADDR_ANY_INIT        {{0}}
#define IN6ADDR_LOOPBACK_INIT   {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}

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

#define INET_ADDRSTRLEN     16
#define INET6_ADDRSTRLEN    46

#endif /* ! _NETINET_IN_H */
