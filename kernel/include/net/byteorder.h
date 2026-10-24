#ifndef _NET_BYTEORDER_H
#define _NET_BYTEORDER_H

#include <core/system.h>

/* Converts between the network's byte order (big-endian) and the CPU's.
 * Every CPU that AquilaOS runs on (x86, aarch64) is little-endian, where
 * they swap the bytes; on a big-endian one they would do nothing. */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define htons(x)    ((uint16_t) (x))
#define htonl(x)    ((uint32_t) (x))
#else
#define htons(x)    ((uint16_t) __builtin_bswap16((uint16_t) (x)))
#define htonl(x)    ((uint32_t) __builtin_bswap32((uint32_t) (x)))
#endif

#define ntohs(x)    htons(x)
#define ntohl(x)    htonl(x)

#endif /* ! _NET_BYTEORDER_H */
