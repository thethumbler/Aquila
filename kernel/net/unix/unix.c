#include <core/system.h>
#include <sys/sched.h>
#include <fs/vfs.h>
#include <ds/ringbuf.h>
#include <net/socket.h>
#include <unix.h>

#define SOCKBUF 8192

static struct sock_ops socket_unix_ops;

static struct queue *sockets = QUEUE_NEW();  /* Open sockets */

int socket_unix_create(struct file *file, int domain, int type, int protocol)
{
    printk("socket_unix_create(file=%p, domain=%d, type=%d, protocol=%d)\n", file, domain, type, protocol);
    struct socket *socket = kmalloc(sizeof(struct socket));

    if (!socket)
        return -ENOMEM;

    memset(socket, 0, sizeof(struct socket));

    socket->domain   = domain;
    socket->type     = type;
    socket->protocol = protocol;
    socket->ops      = &socket_unix_ops;
    socket->p        = NULL;
    file->flags     |= FILE_SOCKET;
    file->socket     = socket;
    return 0;
}

static int socket_unix_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len)
{
    printk("socket_unix_accept(socket=%p, conn=%p, addr=%p, len=%d)\n", file, conn, addr, len);

    struct un_socket *socket = file->socket->p;

    if (!socket->requests.count) {
        /* Sleep until a connection request is present */
        if (thread_queue_sleep(&socket->accept))
            return -EINTR;
    }

    struct un_conn *request = dequeue(&socket->requests);

    /* Initialize connection */
    conn->flags  |= FILE_SOCKET;
    conn->socket  = kmalloc(sizeof(struct socket));
    memcpy(conn->socket, file->socket, sizeof(struct socket));

    conn->socket->domain = AF_UNIX_CONN;
    conn->socket->p      = request;

    /* Initialize buffers */
    request->soci = ringbuf_new(SOCKBUF);
    request->sico = ringbuf_new(SOCKBUF);

    /* Connection established */
    request->connected = 1;

    /* Wakeup client if waiting for connection */
    thread_queue_wakeup(&request->connect);

    return 0;
}

static int socket_unix_bind(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    printk("socket_unix_bind(file=%p, addr=%p, len=%d)\n", file, addr, len);
    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    /* Get path vnode */
    int err = 0;
    struct vnode vnode = {0};
    struct uio uio = PROC_UIO(cur_thread->owner);

    err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL);

    if (!err) /* File exists */
        return -EINVAL;

    /* Create socket */
    if ((err = vfs_mknod(addr_un->sun_path, S_IFSOCK, 0, &uio, NULL)))
        return err;

    /* Get vnode -- FIXME */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL)))
        return err;

    struct un_socket *socket = kmalloc(sizeof(struct un_socket));

    if (!socket)
        return -ENOMEM;

    memset(socket, 0, sizeof(socket));

    memcpy(&socket->vnode, &vnode, sizeof(vnode));

    enqueue(sockets, socket);

    file->socket->p = socket;

    return 0;
}

static int socket_unix_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    printk("socket_unix_connect(socket=%p, addr=%p, len=%d)\n", file, addr, len);

    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    /* Get path vnode */
    int err = 0;
    struct vnode vnode = {0};
    struct uio uio = PROC_UIO(cur_thread->owner);

    /* Open socket */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL))) {
        return err;
    }

    struct un_socket *socket = NULL;

    forlinked(node, sockets->head, node->next) {
        struct un_socket *_socket = (struct un_socket *) node->value;
        if (_socket->vnode.super == vnode.super && _socket->vnode.ino == vnode.ino) {
            socket = _socket;
            break;
        }
    }

    if ((!socket->listening) || socket->requests.count > socket->backlog)
        return -ECONNREFUSED;

    struct un_conn *request = kmalloc(sizeof(struct un_conn));

    if (!request)
        return -ENOMEM;

    memset(request, 0, sizeof(struct un_conn));

    enqueue(&socket->requests, request);

    /* Wake up server if waiting for connections */
    thread_queue_wakeup(&socket->accept);

    /* Sleep until the connection is established */
    if (!request->connected && thread_queue_sleep(&request->connect))
        return -EINTR;

    /* Initialize connection */
    file->socket->domain = AF_UNIX_CONN;
    file->socket->p = request;
    file->offset    = 1;   /* FIXME */

    return 0;
}

static int socket_unix_listen(struct file *file, int backlog)
{
    printk("socket_unix_listen(file=%p, backlog=%d)\n", file, backlog);

    struct un_socket *socket = (struct un_socket *) file->socket->p;

    socket->backlog = backlog;
    socket->listening = 1;

    return 0;
}

static ssize_t socket_unix_send(struct file *file, void *buf, size_t len, int flags)
{
    printk("socket_unix_send(file=%p, buf=%p, len=%d, flags=%x)\n", file, buf, len, flags);

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    struct ringbuf *ring = NULL;

    struct queue *recv_queue = NULL;
    struct queue *send_queue = NULL;

    if (file->offset == 0) {  /* Server */
        ring       = conn->soci;
        recv_queue = &conn->client_recv;
        send_queue = &conn->server_send;
    } else {    /* Client */
        ring       = conn->sico;
        recv_queue = &conn->server_recv;
        send_queue = &conn->client_send;
    }

    ssize_t retval = 0;
    ssize_t incr   = 0;

    for (;;) {
        if (!ring)  /* Connection closed */
            return retval;

        size_t wlen = MIN(len - retval, SOCKBUF - ringbuf_available(ring));

        incr = ringbuf_write(ring, wlen, (char *) buf + retval);

        if (incr > 0)
            thread_queue_wakeup(recv_queue);

        if (incr > 0 && !(flags & MSG_WAITALL))
            return incr;

        retval += incr;

        if ((size_t) retval == len)
            return retval;

        if (thread_queue_sleep(send_queue))
            return -EINTR;
    }
}

static ssize_t socket_unix_recv(struct file *file, void *buf, size_t len, int flags)
{
    printk("socket_unix_recv(file=%p, buf=%p, len=%d, flags=%x)\n", file, buf, len, flags);

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    struct ringbuf *ring = NULL;

    struct queue *recv_queue = NULL;
    struct queue *send_queue = NULL;

    if (file->offset == 0) {  /* Server */
        ring       = conn->sico;
        recv_queue = &conn->server_recv;
        send_queue = &conn->client_send;
    } else {    /* Client */
        ring       = conn->soci;
        recv_queue = &conn->client_recv;
        send_queue = &conn->server_send;
    }

    ssize_t retval = 0;
    ssize_t incr   = 0;

    for (;;) {
        if (!ring)  /* Connection closed */
            return retval;

        incr = ringbuf_read(ring, len - retval, (char *) buf + retval);

        if (incr > 0)
            thread_queue_wakeup(send_queue);

        if (incr > 0 && !(flags & MSG_WAITALL))
            return incr;

        retval += incr;

        if ((size_t) retval == len)
            return retval;

        if (thread_queue_sleep(recv_queue))
            return -EINTR;
    }
}

static int socket_unix_shutdown(struct file *file, int how)
{
    printk("socket_unix_shutdown(file=%p, how=%d)\n", file, how);
    
    if (file->socket->domain == AF_UNIX_CONN) {
        /* Connected socket */
        struct un_conn *conn = file->socket->p;

        int client = file->offset;

        if ((client && (how & SHUT_RD)) || (!client && (how & SHUT_WR))) {
            ringbuf_free(conn->soci);
            conn->soci = NULL;
        }

        if ((client && (how & SHUT_WR)) || (!client && (how & SHUT_RD))) {
            ringbuf_free(conn->sico);
            conn->sico = NULL;
        }

        if (!conn->soci && !conn->sico) {
            /* Free resources */
            //kfree(conn);  /* FIXME */
            kfree(file->socket);
            file->socket = NULL;
        }

        return 0;
    } else {
        /* TODO */
    }

    return -ENOTCONN;
}

static struct sock_ops socket_unix_ops = {
    .accept   = socket_unix_accept,
    .bind     = socket_unix_bind,
    .connect  = socket_unix_connect,
    .listen   = socket_unix_listen,
    .recv     = socket_unix_recv,
    .send     = socket_unix_send,
    .shutdown = socket_unix_shutdown,
};
