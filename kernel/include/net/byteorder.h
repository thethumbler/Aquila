#ifndef _NET_BYTEORDER_H
#define _NET_BYTEORDER_H

#include <core/system.h>

/* the network's byte order (big endian) and x86's */
#define htons(x)    ((uint16_t) __builtin_bswap16((uint16_t) (x)))
#define ntohs(x)    htons(x)
#define htonl(x)    ((uint32_t) __builtin_bswap32((uint32_t) (x)))
#define ntohl(x)    htonl(x)

#endif /* ! _NET_BYTEORDER_H */
