#ifndef _AQUILA_SOCKET_H
#define _AQUILA_SOCKET_H

#include <stdint.h>

/* unsigned int, as programs expect (uint32_t is unsigned long here) */
typedef unsigned int socklen_t;
typedef uint32_t sa_family_t;

/* as big as struct sockaddr_in (20 bytes): programs keep one in it */
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[16];
};

#define SOCK_DGRAM      0x0001
#define SOCK_RAW        0x0002
#define SOCK_SEQPACKET  0x0003
#define SOCK_STREAM     0x0004

#define SOMAXCONN       1024

#define AF_INET         0x0001
#define AF_INET6        0x0002
#define AF_UNIX         0x0003
#define AF_UNSPEC       0x0000  /* any, 0 as programs expect (zeroed hints) */

/* the protocol families, the same */
#define PF_INET         AF_INET
#define PF_INET6        AF_INET6
#define PF_UNIX         AF_UNIX
#define PF_UNSPEC       AF_UNSPEC

#define MSG_CTRUNC      0x0001
#define MSG_DONTROUTE   0x0002
#define MSG_EOR         0x0004
#define MSG_OOB         0x0008
#define MSG_NOSIGNAL    0x0010
#define MSG_PEEK        0x0020
#define MSG_TRUNC       0x0040
#define MSG_WAITALL     0x0080
#define MSG_DONTWAIT    0x0100

/* socket options */
#define SOL_SOCKET      1

#define SO_DEBUG        1
#define SO_REUSEADDR    2
#define SO_TYPE         3
#define SO_ERROR        4
#define SO_DONTROUTE    5
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8
#define SO_KEEPALIVE    9
#define SO_OOBINLINE    10
#define SO_LINGER       13
#define SO_RCVLOWAT     18
#define SO_SNDLOWAT     19
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21
#define SO_ACCEPTCONN   30

#define SHUT_RD         0x0001
#define SHUT_WR         0x0002
#define SHUT_RDWR       (SHUT_RD|SHUT_WR)

#endif /* ! _AQUILA_SOCKET_H */
