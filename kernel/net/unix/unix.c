#include <core/system.h>
#include <sys/sched.h>
#include <fs/vfs.h>
#include <ds/ringbuf.h>
#include <net/socket.h>
#include <sys/uaccess.h>
#include <unix.h>

#include <aquila/fcntl.h>

#define SOCKBUF 8192

MALLOC_DEFINE(M_SOCKET, "socket", "socket struct");
MALLOC_DEFINE(M_UN_SOCKET, "unix socket", "unix socket struct");
MALLOC_DEFINE(M_UN_CONN, "unix conn", "unix connection struct");

static struct sock_ops socket_unix_ops;

static struct queue *sockets = QUEUE_NEW();  /* Open sockets */

/* validate a user supplied AF_UNIX address */
static int unix_addr_check(const struct sockaddr *addr)
{
    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    if (uaccess_check(addr_un, sizeof(sa_family_t)))
        return -EFAULT;

    return uaccess_str_check(addr_un->sun_path);
}

/* wake up everyone sleeping on a connection, so they notice state changes */
static void unix_conn_wakeup(struct un_conn *conn)
{
    thread_queue_wakeup(&conn->server_recv);
    thread_queue_wakeup(&conn->server_send);
    thread_queue_wakeup(&conn->client_recv);
    thread_queue_wakeup(&conn->client_send);
}

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

    if (file->socket->domain != AF_UNIX || !socket || !socket->listening)
        return -EINVAL;

    /* Sleep until a connection request is present */
    while (!socket->requests.count) {
        if (file->flags & O_NONBLOCK)
            return -EAGAIN;

        if (thread_queue_sleep(&socket->accept))
            return -EINTR;
    }

    /* Allocate everything before taking the request off the queue */
    struct socket *conn_socket = kmalloc(sizeof(struct socket), &M_SOCKET, 0);
    struct ringbuf *soci = ringbuf_new(SOCKBUF);
    struct ringbuf *sico = ringbuf_new(SOCKBUF);

    if (!conn_socket || !soci || !sico) {
        kfree(conn_socket);
        if (soci) ringbuf_free(soci);
        if (sico) ringbuf_free(sico);
        return -ENOMEM;
    }

    struct un_conn *request = dequeue(&socket->requests);

    memcpy(conn_socket, file->socket, sizeof(struct socket));

    conn_socket->domain = AF_UNIX_CONN;
    conn_socket->p      = request;
    conn_socket->ref    = 1;    /* only referenced by the new descriptor */

    /* Initialize connection, offset 0 marks the server side */
    conn->socket = conn_socket;
    conn->flags  = FILE_SOCKET;
    conn->offset = 0;

    /* Initialize buffers */
    request->soci = soci;
    request->sico = sico;

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

    if ((err = unix_addr_check(addr)))
        return err;

    /* already bound or connected */
    if (file->socket->domain != AF_UNIX || file->socket->p)
        return -EINVAL;

    struct vnode *vnode = NULL;
    err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL);

    if (err && err != -ENOENT)
        return err;

    if (!err) {
        if (vnode)
            vfs_close(vnode);
        return -EEXIST;
    }

    /* Create socket */
    /* like Linux: 0777 less the umask */
    if ((err = vfs_mknod(addr_un->sun_path, S_IFSOCK | 0777, 0, &uio, NULL)))
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

    if ((err = unix_addr_check(addr)))
        return err;

    if (file->socket->domain != AF_UNIX)
        return -EISCONN;

    /* Open socket */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode, NULL))) {
        return err;
    }

    if (!vnode)
        return -ENOENT;

    struct un_socket *socket = NULL;

    queue_for (node, sockets) {
        struct un_socket *_socket = (struct un_socket *) node->value;
        if (_socket->vnode == vnode) {
            socket = _socket;
            break;
        }
    }

    /* the bound socket holds its own reference to the vnode */
    vfs_close(vnode);

    /* path is not a bound socket, not listening, or backlog full */
    if (!socket || !socket->listening || socket->requests.count >= socket->backlog)
        return -ECONNREFUSED;

    struct un_conn *request = kmalloc(sizeof(struct un_conn), &M_UN_CONN, M_ZERO);
    if (!request) return -ENOMEM;

    enqueue(&socket->requests, request);

    /* Wake up server if waiting for connections */
    thread_queue_wakeup(&socket->accept);

    /* Sleep until the connection is established */
    while (!request->connected) {
        if (thread_queue_sleep(&request->connect))
            return -EINTR;
    }

    /* listening socket was closed before accepting us */
    if (request->connected < 0) {
        kfree(request);
        return -ECONNREFUSED;
    }

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

    /* must be bound (and not a connection) */
    if (file->socket->domain != AF_UNIX || !socket)
        return -EINVAL;

    /* allow at least one pending connection */
    socket->backlog = MIN(MAX(backlog, 1), SOMAXCONN);
    socket->listening = 1;

    return 0;
}

static ssize_t socket_unix_send(struct file *file, void *buf, size_t len, int flags)
{
    //printk("socket_unix_send(file=%p, buf=%p, len=%d, flags=%x)\n", file, buf, len, flags);

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    /* the ring may be freed by the peer shutting down while we sleep, so
     * keep a pointer to the conn field and re-read it every iteration */
    struct ringbuf **ring_p = NULL;

    struct queue *recv_queue = NULL;
    struct queue *send_queue = NULL;

    if (file->offset == 0) {  /* Server */
        ring_p     = &conn->soci;
        recv_queue = &conn->client_recv;
        send_queue = &conn->server_send;
    } else {    /* Client */
        ring_p     = &conn->sico;
        recv_queue = &conn->server_recv;
        send_queue = &conn->client_send;
    }

    /* shut for writing */
    if (file->offset == 0 ? conn->soci_end : conn->sico_end)
        return -EPIPE;

    ssize_t retval = 0;
    ssize_t incr   = 0;

    for (;;) {
        struct ringbuf *ring = *ring_p;

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

        /* full: don't wait if the descriptor doesn't */
        if (file->flags & O_NONBLOCK)
            return retval ? retval : -EAGAIN;

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

    /* see socket_unix_send */
    struct ringbuf **ring_p = NULL;

    struct queue *recv_queue = NULL;
    struct queue *send_queue = NULL;

    int *end_p;

    if (file->offset == 0) {  /* Server */
        ring_p     = &conn->sico;
        end_p      = &conn->sico_end;
        recv_queue = &conn->server_recv;
        send_queue = &conn->client_send;
    } else {    /* Client */
        ring_p     = &conn->soci;
        end_p      = &conn->soci_end;
        recv_queue = &conn->client_recv;
        send_queue = &conn->server_send;
    }

    ssize_t retval = 0;
    ssize_t incr   = 0;

    for (;;) {
        struct ringbuf *ring = *ring_p;

        if (!ring)  /* Connection closed */
            return retval;

        incr = ringbuf_read(ring, len - retval, (char *) buf + retval);

        if (incr > 0)
            thread_queue_wakeup(send_queue);

        /* nothing more will come */
        if (!incr && *end_p)
            return retval;

        if (incr > 0 && !(flags & MSG_WAITALL))
            return incr;

        retval += incr;

        if ((size_t) retval == len)
            return retval;

        /* empty: don't wait if the descriptor doesn't */
        if (file->flags & O_NONBLOCK)
            return retval ? retval : -EAGAIN;

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
        return socket? (int) socket->requests.count : 0;
    }

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    struct ringbuf *ring = NULL;

    int end;

    if (file->offset == 0) {
        /* Server */
        ring = conn->sico;
        end  = conn->sico_end;
    } else {
        /* Client */
        ring = conn->soci;
        end  = conn->soci_end;
    }

    if (!ring)
        return -ENOTCONN; /* ??? */

    /* the end can be read */
    size_t available = ringbuf_available(ring);
    return available ? (int) available : end;
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

    if (!ring)
        return -ENOTCONN;

    return ring->size - ringbuf_available(ring);
}


/* a connection is shut for reading or writing (or both): the ring read
 * goes (the peer can't write), the ring written ends (the peer reads what
 * is left, then the end) */
static void unix_conn_shut(struct file *file, int how)
{
    struct un_conn *conn = file->socket->p;
    int client = file->offset;

    struct ringbuf **in = client ? &conn->soci : &conn->sico;
    int *out_end = client ? &conn->sico_end : &conn->soci_end;

    if ((how & SHUT_RD) && *in) {
        ringbuf_free(*in);
        *in = NULL;
    }

    if (how & SHUT_WR)
        *out_end = 1;

    /* wake up the peer, blocked on a ring that went or ended */
    unix_conn_wakeup(conn);
}

/* shutdown(), of a connection only */
static int socket_unix_shut(struct file *file, int how)
{
    if (!file || !file->socket)
        return -EINVAL;

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    unix_conn_shut(file, how);
    return 0;
}

static int socket_unix_shutdown(struct file *file, int how)
{
    //printk("socket_unix_shutdown(file=%p, how=%d)\n", file, how);
    
    if (!file || !file->socket)
        return -EINVAL;
    
    if (file->socket->domain == AF_UNIX_CONN) {
        /* Connected socket */
        struct un_conn *conn = file->socket->p;

        unix_conn_shut(file, how);

        /* this side is gone; the connection once both are (each frees
         * the ring it read) */
        if (!conn->soci && !conn->sico)
            kfree(conn);

        kfree(file->socket);
        file->socket = NULL;

        return 0;
    }

    /* Unconnected socket, unbind it if bound */
    struct un_socket *socket = file->socket->p;

    if (socket) {
        struct un_conn *request;

        queue_remove(sockets, socket);

        /* refuse pending connections, connect() frees the request */
        while ((request = dequeue(&socket->requests))) {
            request->connected = -1;
            thread_queue_wakeup(&request->connect);
        }

        vfs_close(socket->vnode);
        kfree(socket);
    }

    kfree(file->socket);
    file->socket = NULL;

    return 0;
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
    .shut      = socket_unix_shut,
};
