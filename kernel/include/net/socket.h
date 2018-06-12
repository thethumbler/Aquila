#ifndef _SOCKET_H
#define _SOCKET_H

#include <core/system.h>
#include <fs/vfs.h>

typedef uint32_t socklen_t;
typedef uint32_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[];
};

struct socket {
    int domain;
    int type;
    int protocol;

    /* Socket handler */
    struct sock_ops *ops;

    /* Private data */
    void *p;
};

struct sock_ops {
    int (*accept) (struct file *socket, struct file *conn, const struct sockaddr *addr, socklen_t *len);
    int (*bind)   (struct file *socket, const struct sockaddr *addr, socklen_t len);
    int (*connect)(struct file *socket, const struct sockaddr *addr, socklen_t len);
    int (*listen) (struct file *socket, int backlog);

    ssize_t (*recv)(struct file *socket, void *buf, size_t len, int flags);
    ssize_t (*send)(struct file *socket, void *buf, size_t len, int flags);

    int (*shutdown) (struct file *socket, int how);
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

#define FILE_SOCKET     0x80000000

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

int socket_create(struct file *file, int domain, int type, int protocol);
int socket_bind(struct file *file, const struct sockaddr *addr, uint32_t len);
int socket_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len);
int socket_connect(struct file *file, const struct sockaddr *addr, uint32_t len);
int socket_listen(struct file *file, int backlog);
int socket_send(struct file *file, void *buf, size_t len, int flags);
int socket_recv(struct file *file, void *buf, size_t len, int flags);
int socket_shutdown(struct file *file, int how);

/* AF_UNIX */
int socket_unix_create(struct file *file, int domain, int type, int protocol);

#endif /* !_SOCKET_H */
