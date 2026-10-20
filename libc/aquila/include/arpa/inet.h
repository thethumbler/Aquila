#ifndef _ARPA_INET_H
#define _ARPA_INET_H

/*
 * Byte order conversion and Internet address types. There is no network
 * stack yet, so the address conversion functions (inet_addr, inet_ntop,
 * ...) are not provided.
 */

#include <stdint.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

/* i386 is little-endian, network byte order is big-endian */
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }

#endif /* ! _ARPA_INET_H */
