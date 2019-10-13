#include <core/system.h>
#include <sys/sched.h>
#include <fs/vfs.h>
#include <ds/ringbuf.h>
#include <net/socket.h>
#include <unix.h>

#define SOCKBUF 8192

MALLOC_DEFINE(M_SOCKET, "socket", "socket struct");
MALLOC_DEFINE(M_UN_SOCKET, "unix socket", "unix socket struct");
MALLOC_DEFINE(M_UN_CONN, "unix conn", "unix connection struct");

static struct sock_ops socket_unix_ops;

static struct queue *sockets = QUEUE_NEW();  /* Open sockets */

int socket_unix_create(struct file *file, int domain, int type, int protocol)
{
    //printk("socket_unix_create(file=%p, domain=%d, type=%d, protocol=%d)\n", file, domain, type, protocol);
    struct socket *socket = kmalloc(sizeof(struct socket), &M_SOCKET, M_ZERO);
    if (!socket) return -ENOMEM;

    socket->domain   = domain;
    socket->type     = type;
    socket->protocol = protocol;
    socket->ops      = &socket_unix_ops;
    socket->p        = NULL;
    socket->ref      = 1;

    file->flags     |= FILE_SOCKET;
    file->socket     = socket;
    return 0;
}

static int socket_unix_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len)
{
    //printk("%d:%d socket_unix_accept(socket=%p, conn=%p, addr=%p, len=%p)\n", curthread->tid, curproc->pid, file, conn, addr, len);
    
    if (!file || !file->socket)
        return -EINVAL;

    struct un_socket *socket = file->socket->p;

    if (!socket)
        return -EINVAL;

    if (!socket->requests.count) {
        /* Sleep until a connection request is present */
        if (thread_queue_sleep(&socket->accept))
            return -EINTR;
    }

    struct un_conn *request = dequeue(&socket->requests);

    /* Initialize connection */
    conn->flags  |= FILE_SOCKET;
    conn->socket  = kmalloc(sizeof(struct socket), &M_SOCKET, 0);
    if (!conn->socket) {
        /* TODO */
    }

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
    //printk("socket_unix_bind(file=%p, addr=%p, len=%d)\n", file, addr, len);
    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    /* Get path vnode */
    int err = 0;
    struct uio uio = PROC_UIO(curproc);

    struct vnode *vnode = NULL;
    err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL);

    if (err && err != -ENOENT)
        return err;

    if (!err)
        return -EEXIST;

    /* Create socket */
    if ((err = vfs_mknod(addr_un->sun_path, S_IFSOCK, 0, &uio, NULL)))
        return err;

    /* Get vnode -- FIXME */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL)))
        return err;

    struct un_socket *socket = kmalloc(sizeof(struct un_socket), &M_UN_SOCKET, M_ZERO);
    if (!socket) return -ENOMEM;

    socket->vnode = vnode;
    enqueue(sockets, socket);

    file->socket->p = socket;

    return 0;
}

static int socket_unix_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    //printk("socket_unix_connect(socket=%p, addr=%p, len=%d)\n", file, addr, len);

    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    /* Get path vnode */
    int err = 0;
    struct vnode *vnode = NULL;
    struct uio uio = PROC_UIO(curproc);

    /* Open socket */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL))) {
        return err;
    }

    struct un_socket *socket = NULL;

    queue_for (node, sockets) {
        struct un_socket *_socket = (struct un_socket *) node->value;
        if (_socket->vnode == vnode) {
            socket = _socket;
            break;
        }
    }

    if ((!socket->listening) || socket->requests.count > socket->backlog)
        return -ECONNREFUSED;

    struct un_conn *request = kmalloc(sizeof(struct un_conn), &M_UN_CONN, M_ZERO);
    if (!request) return -ENOMEM;

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
    //printk("socket_unix_listen(file=%p, backlog=%d)\n", file, backlog);

    struct un_socket *socket = (struct un_socket *) file->socket->p;

    socket->backlog = backlog;
    socket->listening = 1;

    return 0;
}

static ssize_t socket_unix_send(struct file *file, void *buf, size_t len, int flags)
{
    //printk("socket_unix_send(file=%p, buf=%p, len=%d, flags=%x)\n", file, buf, len, flags);

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
    //printk("socket_unix_recv(file=%p, buf=%p, len=%d, flags=%x)\n", file, buf, len, flags);

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

static int socket_unix_can_read(struct file *file, size_t len)
{
    //printk("socket_unix_can_read(file=%p, len=%d)\n", file, len);

    if (!file || !file->socket)
        return -EINVAL;

    if (file->socket->domain == AF_UNIX) {
        struct un_socket *socket = file->socket->p;
        return socket->requests.count;
    }

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    struct ringbuf *ring = NULL;

    if (file->offset == 0) {
        /* Server */
        ring = conn->sico;
    } else {
        /* Client */
        ring = conn->soci;
    }

    if (!ring)
        return -ENOTCONN; /* ??? */

    return ringbuf_available(ring);
}

static int socket_unix_can_write(struct file *file, size_t len)
{
    //printk("socket_unix_can_write(file=%p, len=%d)\n", file, len);

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    struct ringbuf *ring = NULL;

    if (file->offset == 0) {
        /* Server */
        ring = conn->soci;
    } else {
        /* Client */
        ring = conn->sico;
    }

    return ring->size - ringbuf_available(ring);
}


static int socket_unix_shutdown(struct file *file, int how)
{
    //printk("socket_unix_shutdown(file=%p, how=%d)\n", file, how);
    
    if (!file || !file->socket)
        return -EINVAL;
    
    if (file->socket->domain == AF_UNIX_CONN) {
        /* Connected socket */
        struct un_conn *conn = file->socket->p;

        int client = file->offset;

        if ((client && (how & SHUT_RD)) || (!client && (how & SHUT_WR))) {
            if (conn->soci) {
                ringbuf_free(conn->soci);
                conn->soci = NULL;
            }
        }

        if ((client && (how & SHUT_WR)) || (!client && (how & SHUT_RD))) {
            if (conn->sico) {
                ringbuf_free(conn->sico);
                conn->sico = NULL;
            }
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
    .accept    = socket_unix_accept,
    .bind      = socket_unix_bind,
    .connect   = socket_unix_connect,
    .listen    = socket_unix_listen,
    .recv      = socket_unix_recv,
    .send      = socket_unix_send,
    .can_read  = socket_unix_can_read,
    .can_write = socket_unix_can_write,
    .shutdown  = socket_unix_shutdown,
};
