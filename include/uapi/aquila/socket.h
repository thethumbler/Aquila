#ifndef _AQUILA_SOCKET_H
#define _AQUILA_SOCKET_H

#include <stdint.h>

typedef uint32_t socklen_t;
typedef uint32_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[];
};

#define SOCK_DGRAM      0x0001
#define SOCK_RAW        0x0002
#define SOCK_SEQPACKET  0x0003
#define SOCK_STREAM     0x0004

#define SOMAXCONN       1024

#define AF_INET         0x0001
#define AF_INET6        0x0002
#define AF_UNIX         0x0003
#define AF_UNSPEC       0x0004

#define MSG_CTRUNC      0x0001
#define MSG_DONTROUTE   0x0002
#define MSG_EOR         0x0004
#define MSG_OOB         0x0008
#define MSG_NOSIGNAL    0x0010
#define MSG_PEEK        0x0020
#define MSG_TRUNC       0x0040
#define MSG_WAITALL     0x0080

#define SHUT_RD         0x0001
#define SHUT_WR         0x0002
#define SHUT_RDWR       (SHUT_RD|SHUT_WR)

#endif /* ! _AQUILA_SOCKET_H */
