/**
 * \defgroup net kernel/net
 * \brief networking subsystem
 */

#include <core/system.h>
#include <core/string.h>
#include <ds/queue.h>
#include <aquila/errno.h>
#include <net/socket.h>

int socket_create(struct file *file, int domain, int type, int protocol)
{
    switch (domain) {
        case AF_UNIX:
            return socket_unix_create(file, domain, type, protocol);
        case AF_INET:
            return socket_inet_create(file, domain, type, protocol);
    }

    return -EAFNOSUPPORT;
}

int socket_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->accept)
        return -EOPNOTSUPP;

    return file->socket->ops->accept(file, conn, addr, len);
}

int socket_bind(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->bind)
        return -EOPNOTSUPP;

    return file->socket->ops->bind(file, addr, len);
}

int socket_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->connect)
        return -EOPNOTSUPP;

    return file->socket->ops->connect(file, addr, len);
}

int socket_listen(struct file *file, int backlog)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->listen)
        return -EOPNOTSUPP;

    return file->socket->ops->listen(file, backlog);
}

int socket_send(struct file *file, void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    if (!file)
        return -EINVAL;

    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!(file->socket))
        return -EINVAL;

    if (!file->socket->ops || !file->socket->ops->send)
        return -EOPNOTSUPP;

    return file->socket->ops->send(file, buf, len, flags, to, tolen);
}

int socket_recv(struct file *file, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->recv)
        return -EOPNOTSUPP;

    return file->socket->ops->recv(file, buf, len, flags, from, fromlen);
}

int socket_can_read(struct file *file, size_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->can_read)
        return -EOPNOTSUPP;

    return file->socket->ops->can_read(file, len);
}

int socket_can_write(struct file *file, size_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->can_write)
        return -EOPNOTSUPP;

    return file->socket->ops->can_write(file, len);
}

/* shutdown(): no more reading or writing, the socket stays open */
int socket_shutdown_dir(struct file *file, int how)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (how & ~SHUT_RDWR || !how)
        return -EINVAL;

    if (!file->socket->ops || !file->socket->ops->shut)
        return -EOPNOTSUPP;

    return file->socket->ops->shut(file, how);
}

int socket_shutdown(struct file *file, int how)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->shutdown)
        return -EOPNOTSUPP;

    file->socket->ref--;

    if (!file->socket->ref)
        return file->socket->ops->shutdown(file, how);

    return 0;
}

int socket_getname(struct file *file, struct sockaddr *addr, socklen_t *len, int peer)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->getname)
        return -EOPNOTSUPP;

    return file->socket->ops->getname(file, addr, len, peer);
}

/* an int option's value */
static int sockopt_int(void *val, socklen_t *len, int v)
{
    if (*len < sizeof(int))
        return -EINVAL;

    *(int *) val = v;
    *len = sizeof(int);
    return 0;
}

int socket_getsockopt(struct file *file, int level, int name, void *val, socklen_t *len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    struct sock_ops *ops = file->socket->ops;

    if (level == SOL_SOCKET && name == SO_TYPE)
        return sockopt_int(val, len, file->socket->type);

    if (ops && ops->getsockopt) {
        int err = ops->getsockopt(file, level, name, val, len);

        if (err != -ENOPROTOOPT)
            return err;
    }

    /* no pending error, if the protocol doesn't keep them */
    if (level == SOL_SOCKET && name == SO_ERROR)
        return sockopt_int(val, len, 0);

    return -ENOPROTOOPT;
}

int socket_setsockopt(struct file *file, int level, int name, const void *val, socklen_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    struct sock_ops *ops = file->socket->ops;

    if (ops && ops->setsockopt) {
        int err = ops->setsockopt(file, level, name, val, len);

        if (err != -ENOPROTOOPT)
            return err;
    }

    /* accepted, without effect */
    if (level == SOL_SOCKET) {
        switch (name) {
            case SO_REUSEADDR:
            case SO_KEEPALIVE:
            case SO_SNDBUF:
            case SO_RCVBUF:
            case SO_LINGER:
            case SO_SNDTIMEO:
                return 0;
        }
    }

    return -ENOPROTOOPT;
}

int socket_ioctl(struct file *file, int request, void *argp)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->ioctl)
        return -ENOTTY;

    return file->socket->ops->ioctl(file, request, argp);
}
