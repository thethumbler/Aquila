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

    /* recvfrom(), sendto(): `from' and `to' NULL if the caller doesn't
     * want, or doesn't give, an address */
    ssize_t (*recv)(struct file *socket, void *buf, size_t len, int flags,
                    struct sockaddr *from, socklen_t *fromlen);
    ssize_t (*send)(struct file *socket, void *buf, size_t len, int flags,
                    const struct sockaddr *to, socklen_t tolen);

    /* its address, or its peer's */
    int (*getname)(struct file *socket, struct sockaddr *addr, socklen_t *len, int peer);

    /* options of a level other than SOL_SOCKET's own (the protocol's),
     * and those of SOL_SOCKET the socket layer doesn't answer */
    int (*getsockopt)(struct file *socket, int level, int name, void *val, socklen_t *len);
    int (*setsockopt)(struct file *socket, int level, int name, const void *val, socklen_t len);

    /* ioctl(): FIONREAD, and the protocol's (the interfaces') */
    int (*ioctl)(struct file *socket, int request, void *argp);

    int (*can_read)(struct file *socket, size_t len);
    int (*can_write)(struct file *socket, size_t len);

    /* the last descriptor is closed */
    int (*shutdown) (struct file *socket, int how);

    /* shutdown(): no more reading or writing (SHUT_*), it stays open */
    int (*shut) (struct file *socket, int how);
};

#define FILE_SOCKET     0x80000000

int socket_create(struct file *file, int domain, int type, int protocol);
int socket_bind(struct file *file, const struct sockaddr *addr, socklen_t len);
int socket_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len);
int socket_connect(struct file *file, const struct sockaddr *addr, socklen_t len);
int socket_listen(struct file *file, int backlog);
int socket_send(struct file *file, void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
int socket_recv(struct file *file, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int socket_getname(struct file *file, struct sockaddr *addr, socklen_t *len, int peer);
int socket_getsockopt(struct file *file, int level, int name, void *val, socklen_t *len);
int socket_setsockopt(struct file *file, int level, int name, const void *val, socklen_t len);
int socket_ioctl(struct file *file, int request, void *argp);
int socket_can_read(struct file *file, size_t len);
int socket_can_write(struct file *file, size_t len);
int socket_shutdown(struct file *file, int how);
int socket_shutdown_dir(struct file *file, int how);

/* AF_UNIX */
int socket_unix_create(struct file *file, int domain, int type, int protocol);

/* AF_INET */
int socket_inet_create(struct file *file, int domain, int type, int protocol);

#endif /* ! _NET_SOCKET_H */
