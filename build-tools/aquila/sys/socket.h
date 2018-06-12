#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <bits/socket.h>

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

int socket(int domain, int type, int protocol);
int accept(int fd, const struct sockaddr *addr, uint32_t len);
int bind(int fd, const struct sockaddr *addr, uint32_t len);
int connect(int fd, const struct sockaddr *addr, uint32_t len);
int listen(int fd, int backlog);
ssize_t send(int fd, void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#endif /* ! _SYS_SOCKET_H */
