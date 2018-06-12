#include <core/system.h>
#include <core/string.h>
#include <ds/queue.h>
#include <bits/errno.h>
#include <net/socket.h>

int socket_create(struct file *file, int domain, int type, int protocol)
{
    switch (domain) {
        case AF_UNIX:
            return socket_unix_create(file, domain, type, protocol);
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

int socket_bind(struct file *file, const struct sockaddr *addr, uint32_t len)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->bind)
        return -EOPNOTSUPP;

    return file->socket->ops->bind(file, addr, len);
}

int socket_connect(struct file *file, const struct sockaddr *addr, uint32_t len)
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

int socket_send(struct file *file, void *buf, size_t len, int flags)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->send)
        return -EOPNOTSUPP;

    return file->socket->ops->send(file, buf, len, flags);
}

int socket_recv(struct file *file, void *buf, size_t len, int flags)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->recv)
        return -EOPNOTSUPP;

    return file->socket->ops->recv(file, buf, len, flags);
}

int socket_shutdown(struct file *file, int how)
{
    if (!(file->flags & FILE_SOCKET))
        return -ENOTSOCK;

    if (!file->socket->ops || !file->socket->ops->shutdown)
        return -EOPNOTSUPP;

    return file->socket->ops->shutdown(file, how);
}
