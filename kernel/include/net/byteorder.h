#ifndef _NET_BYTEORDER_H
#define _NET_BYTEORDER_H

#include <core/system.h>

/* Converts between the network's byte order (big-endian) and the CPU's.
 * They always swap: every CPU that AquilaOS runs on (x86, aarch64) is
 * little-endian. */
#define htons(x)    ((uint16_t) __builtin_bswap16((uint16_t) (x)))
#define ntohs(x)    htons(x)
#define htonl(x)    ((uint32_t) __builtin_bswap32((uint32_t) (x)))
#define ntohl(x)    htonl(x)

#endif /* ! _NET_BYTEORDER_H */
