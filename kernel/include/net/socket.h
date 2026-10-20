#ifndef _NET_SOCKET_H
#define _NET_SOCKET_H

#include <core/system.h>
#include <fs/vfs.h>

#include <aquila/socket.h>

struct socket {
    int domain;
    int type;
    int protocol;

    /* Socket handler */
    struct sock_ops *ops;

    /* Private data */
    void *p;

    ssize_t ref;
};

struct sock_ops {
    int (*accept) (struct file *socket, struct file *conn, const struct sockaddr *addr, socklen_t *len);
    int (*bind)   (struct file *socket, const struct sockaddr *addr, socklen_t len);
    int (*connect)(struct file *socket, const struct sockaddr *addr, socklen_t len);
    int (*listen) (struct file *socket, int backlog);

    ssize_t (*recv)(struct file *socket, void *buf, size_t len, int flags);
    ssize_t (*send)(struct file *socket, void *buf, size_t len, int flags);

    int (*can_read)(struct file *socket, size_t len);
    int (*can_write)(struct file *socket, size_t len);

    int (*shutdown) (struct file *socket, int how);
};

#define FILE_SOCKET     0x80000000

int socket_create(struct file *file, int domain, int type, int protocol);
int socket_bind(struct file *file, const struct sockaddr *addr, uint32_t len);
int socket_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len);
int socket_connect(struct file *file, const struct sockaddr *addr, uint32_t len);
int socket_listen(struct file *file, int backlog);
int socket_send(struct file *file, void *buf, size_t len, int flags);
int socket_recv(struct file *file, void *buf, size_t len, int flags);
int socket_can_read(struct file *file, size_t len);
int socket_can_write(struct file *file, size_t len);
int socket_shutdown(struct file *file, int how);

/* AF_UNIX */
int socket_unix_create(struct file *file, int domain, int type, int protocol);

#endif /* ! _NET_SOCKET_H */
