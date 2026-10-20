#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

/* address families, socket types and struct sockaddr, shared with the kernel */
#include <aquila/socket.h>

/* any address (the kernel's are AF_UNIX) */
struct sockaddr_storage {
    sa_family_t ss_family;
    char __ss_data[124];
} __attribute__((aligned(8)));

/* socket options: the kernel has none, they are accepted (setsockopt) and
 * SO_ERROR and SO_TYPE answered (getsockopt) */
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

struct linger {
    int l_onoff;
    int l_linger;
};

int socket(int domain, int type, int protocol);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

/* of connected sockets: the address is the peer */
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *addr, socklen_t *addrlen);

int shutdown(int fd, int how);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int getsockopt(int fd, int level, int name, void *val, socklen_t *len);
int setsockopt(int fd, int level, int name, const void *val, socklen_t len);

#endif /* ! _SYS_SOCKET_H */
