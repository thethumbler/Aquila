#include <core/system.h>
#include <aquila/ioctl.h>
#include <sys/sched.h>
#include <fs/vfs.h>
#include <ds/ringbuf.h>
#include <net/socket.h>
#include <sys/uaccess.h>
#include <unix.h>

#include <aquila/fcntl.h>

/* what a connection holds each way: X servers send replies of hundreds of
 * KB (font metrics), each fill of the ring is a switch between processes */
#define SOCKBUF 65536

MALLOC_DEFINE(M_SOCKET, "socket", "socket struct");
MALLOC_DEFINE(M_UN_SOCKET, "unix socket", "unix socket struct");
MALLOC_DEFINE(M_UN_CONN, "unix conn", "unix connection struct");

static struct sock_ops socket_unix_ops;

static struct queue *sockets = QUEUE_NEW();  /* Open sockets */

/** an AF_UNIX address (the kernel's copy, as big as a sockaddr_un): its
 * path ends in it */
static int unix_addr_check(const struct sockaddr *addr)
{
    struct sockaddr_un *addr_un = (struct sockaddr_un *) addr;

    return strnlen(addr_un->sun_path, sizeof(addr_un->sun_path)) < sizeof(addr_un->sun_path) ? 0 : -EINVAL;
}

/** wake up everyone sleeping on a connection, so they notice state changes */
static void unix_conn_wakeup(struct un_conn *conn)
{
    waitq_wakeup(&conn->server_recv);
    waitq_wakeup(&conn->server_send);
    waitq_wakeup(&conn->client_recv);
    waitq_wakeup(&conn->client_send);
}

int socket_unix_create(struct file *file, int domain, int type, int protocol)
{
    /* streams only: a datagram socket would be one too, silently */
    if (type != SOCK_STREAM)
        return -EPROTONOSUPPORT;

    if (protocol)
        return -EPROTONOSUPPORT;

    struct socket *socket = kmalloc(sizeof(struct socket), &M_SOCKET, M_ZERO);
    if (!socket) return -ENOMEM;

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
    if (!file || !file->socket)
        return -EINVAL;

    struct un_socket *socket = file->socket->p;

    if (file->socket->domain != AF_UNIX || !socket || !socket->listening)
        return -EINVAL;

    /* Sleep until a connection request is present */
    while (!socket->requests.count) {
        if (file->flags & O_NONBLOCK)
            return -EAGAIN;

        int err = waitq_sleep(&socket->accept);

        if (err)
            return err;
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
    waitq_wakeup(&request->connect);

    return 0;
}

static int socket_unix_bind(struct file *file, const struct sockaddr *addr, socklen_t len)
{
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
    err = vfs_lookup(addr_un->sun_path, &uio, &vnode);

    if (err && err != -ENOENT)
        return err;

    if (!err) {
        if (vnode)
            vfs_close(vnode);

        /* the path is taken, as POSIX says for bind() */
        return -EADDRINUSE;
    }

    /* Create socket */
    /* like Linux: 0777 less the umask */
    if ((err = vfs_mknod(addr_un->sun_path, S_IFSOCK | 0777, 0, &uio, NULL)))
        return err;

    /* Get vnode -- FIXME */
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode)))
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
    if ((err = vfs_lookup(addr_un->sun_path, &uio, &vnode))) {
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
    waitq_wakeup(&socket->accept);

    /* Sleep until the connection is established */
    while (!request->connected) {
        int err = waitq_sleep(&request->connect);

        if (err)
            return err;
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
    struct un_socket *socket = (struct un_socket *) file->socket->p;

    /* must be bound (and not a connection) */
    if (file->socket->domain != AF_UNIX || !socket)
        return -EINVAL;

    /* allow at least one pending connection */
    socket->backlog = MIN(MAX(backlog, 1), SOMAXCONN);
    socket->listening = 1;

    return 0;
}

/**
 * Between a connection's ring and an iobuf, a chunk at a time through the
 * kernel: copying to or from user memory may sleep (a page brought in
 * from the disk), and the peer may free the ring meanwhile (*ring_p NULL
 * then). How many bytes moved, -EFAULT if a fault came first.
 */
static ssize_t ring_to_iobuf(struct ringbuf **ring_p, size_t n, struct iobuf *io)
{
    char chunk[256];
    size_t done = 0;

    while (done < n && *ring_p) {
        size_t len = ringbuf_read_noconsume(*ring_p, 0, MIN(n - done, sizeof(chunk)), chunk);

        if (!len)
            break;

        ssize_t r = iobuf_out(io, chunk, len);

        if (r < 0)
            return done ? (ssize_t) done : r;

        /* out: off the ring */
        if (*ring_p)
            ringbuf_discard(*ring_p, r);

        done += r;

        if ((size_t) r < len)
            break;
    }

    return done;
}

static ssize_t iobuf_to_ring(struct ringbuf **ring_p, size_t n, struct iobuf *io)
{
    char chunk[256];
    size_t done = 0;

    while (done < n && *ring_p) {
        ssize_t len = iobuf_peek(io, chunk, MIN(n - done, sizeof(chunk)));

        if (len <= 0)
            return done ? (ssize_t) done : len;

        if (!*ring_p)
            break;

        size_t w = ringbuf_write(*ring_p, len, chunk);
        iobuf_skip(io, w);
        done += w;

        if (w < (size_t) len || io->fault)
            break;
    }

    return done;
}

static ssize_t socket_unix_send(struct file *file, struct iobuf *io, int flags,
                                const struct sockaddr *to, socklen_t tolen)
{
    size_t len = io->resid;

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    struct un_conn *conn = file->socket->p;

    /* the ring may be freed by the peer shutting down while we sleep, so
     * keep a pointer to the conn field and re-read it every iteration */
    struct ringbuf **ring_p = NULL;

    struct waitq *recv_queue = NULL;
    struct waitq *send_queue = NULL;

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

        if ((incr = iobuf_to_ring(ring_p, wlen, io)) < 0)
            return retval ? retval : incr;

        if (incr > 0)
            waitq_wakeup(recv_queue);

        if (incr > 0 && !(flags & MSG_WAITALL))
            return incr;

        retval += incr;

        if ((size_t) retval == len || io->fault)
            return retval;

        /* full: don't wait if the descriptor doesn't */
        if (file->flags & O_NONBLOCK)
            return retval ? retval : -EAGAIN;

        int err = waitq_sleep(send_queue);

        if (err)
            return retval ? retval : err;
    }
}

static ssize_t socket_unix_recv(struct file *file, struct iobuf *io, int flags,
                                struct sockaddr *from, socklen_t *fromlen)
{
    size_t len = io->resid;

    if (file->socket->domain != AF_UNIX_CONN)
        return -ENOTCONN;

    /* the peer, unnamed (names aren't kept) */
    if (from && fromlen) {
        if (*fromlen >= sizeof(sa_family_t))
            from->sa_family = AF_UNIX;
        *fromlen = sizeof(sa_family_t);
    }

    struct un_conn *conn = file->socket->p;

    /* see socket_unix_send */
    struct ringbuf **ring_p = NULL;

    struct waitq *recv_queue = NULL;
    struct waitq *send_queue = NULL;

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

        if ((incr = ring_to_iobuf(ring_p, len - retval, io)) < 0)
            return retval ? retval : incr;

        if (incr > 0)
            waitq_wakeup(send_queue);

        /* nothing more will come */
        if (!incr && *end_p)
            return retval;

        if (incr > 0 && !(flags & MSG_WAITALL))
            return incr;

        retval += incr;

        if ((size_t) retval == len || io->fault)
            return retval;

        /* empty: don't wait if the descriptor doesn't */
        if (file->flags & O_NONBLOCK)
            return retval ? retval : -EAGAIN;

        int err = waitq_sleep(recv_queue);

        if (err)
            return retval ? retval : err;
    }
}

static int socket_unix_can_read(struct file *file, size_t len)
{
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

/** a listener's accept waits on accept; a connection's recv and send on
 * the queues of its side (socket_unix_recv, socket_unix_send) */
static struct waitq *socket_unix_wait_queue(struct file *file, int write)
{
    if (file->socket->domain == AF_UNIX) {
        struct un_socket *socket = file->socket->p;
        return socket && !write ? &socket->accept : NULL;
    }

    if (file->socket->domain != AF_UNIX_CONN)
        return NULL;

    struct un_conn *conn = file->socket->p;

    if (file->offset == 0)  /* Server */
        return write ? &conn->server_send : &conn->server_recv;
    else                    /* Client */
        return write ? &conn->client_send : &conn->client_recv;
}

static int socket_unix_can_write(struct file *file, size_t len)
{
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


/** a connection is shut for reading or writing (or both): the ring read
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

/** shutdown(), of a connection only */
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
            waitq_wakeup(&request->connect);
        }

        vfs_close(socket->vnode);
        kfree(socket);
    }

    kfree(file->socket);
    file->socket = NULL;

    return 0;
}

/** its name or its peer's: unnamed (names aren't kept) */
static int socket_unix_getname(struct file *file, struct sockaddr *addr, socklen_t *len, int peer)
{
    if (*len >= sizeof(sa_family_t))
        addr->sa_family = AF_UNIX;

    *len = sizeof(sa_family_t);
    return 0;
}

static int socket_unix_ioctl(struct file *file, int request, void *argp)
{
    if (request == FIONREAD) {
        int n = socket_unix_can_read(file, 0);

        /* a listening socket has connections, not bytes; the end is 0 */
        n = file->socket->domain == AF_UNIX_CONN && n > 0 ? n : 0;
        return copyout(argp, &n, sizeof(n));
    }

    return -ENOTTY;
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
    .wait_queue = socket_unix_wait_queue,
    .shutdown  = socket_unix_shutdown,
    .shut      = socket_unix_shut,
    .getname   = socket_unix_getname,
    .ioctl     = socket_unix_ioctl,
};
