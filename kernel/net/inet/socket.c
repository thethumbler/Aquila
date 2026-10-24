/** \file
 * `AF_INET` datagram sockets: UDP (`SOCK_DGRAM`), and ping sockets
 * (`SOCK_DGRAM` with `IPPROTO_ICMP`), which send echo requests and
 * receive the replies, without needing root. TCP sockets are in
 * `tcp_socket.c`.
 *
 * A socket bound to a port (for a ping socket, the identifier of its
 * echoes) is on the list of bound sockets, where the protocols look up
 * the socket a datagram is for. Received datagrams wait in the socket's
 * queue, with where they came from, up to `RECV_MAX` bytes of buffers;
 * more are dropped.
 */

#include <core/system.h>
#include <dev/random.h>
#include <core/string.h>
#include <core/arch.h>
#include <ds/queue.h>
#include <fs/vfs.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <net/socket.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/uaccess.h>

#include <aquila/errno.h>
#include <aquila/fcntl.h>
#include <aquila/in.h>
#include <aquila/ioctl.h>

MALLOC_DECLARE(M_SOCKET);
MALLOC_DEFINE(M_INET_SOCK, "inet socket", "AF_INET socket");

/* What a socket's queue may hold, counted in buffers: each datagram costs
 * its whole netbuf, whatever its size. If datagrams were counted by their
 * bytes, empty ones would cost nothing, and a process could fill all of
 * memory with them. */
#define RECV_MAX        (128 * NETBUF_SIZE)

#define PORT_FIRST      49152   /* the ephemeral ports' range */
#define PORT_LAST       65535

/** A UDP or ping socket. */
struct inet_sock {
    uint8_t proto;              /* IP_PROTO_UDP or IP_PROTO_ICMP */

    /* addresses and ports in network byte order; it isn't bound while
     * lport is 0 */
    uint32_t laddr;
    uint16_t lport;
    uint32_t raddr;
    uint16_t rport;
    int connected;

    struct netbuf *recv_head, *recv_tail;
    size_t recv_bytes;
    struct waitq recv_wait;

    struct netif *netif;        /* SO_BINDTODEVICE: only use this interface */

    int error;                  /* an error to report (SO_ERROR, or the next call) */
    uint64_t rcvtimeo;          /* the receive timeout in ns, or 0 for none */
    int broadcast;              /* SO_BROADCAST */
    int shut_rd;

    struct inet_sock *next;     /* the next bound socket */
};

static struct inet_sock *bound;

static struct sock_ops socket_inet_ops;

/*
 * The list of bound sockets
 */

/** Returns whether a socket of `proto` is bound to `port` on `addr` (or on
 * any address, or `addr` is any). */
static int port_used(uint8_t proto, uint32_t addr, uint16_t port)
{
    for (struct inet_sock *s = bound; s; s = s->next)
        if (s->proto == proto && s->lport == port &&
            (!s->laddr || !addr || s->laddr == addr))
            return 1;

    return 0;
}

/** Binds a socket to an address and port, and puts it on the list of bound
 * sockets. Port 0 picks a free ephemeral port. Returns 0, or
 * `-EADDRINUSE`. */
static int sock_bind(struct inet_sock *sock, uint32_t addr, uint16_t port)
{
    if (!port) {
        /* the first free port from a random one on (RFC 6056, algorithm
         * 1), so that the next port isn't the last one plus one, for
         * anyone to guess */
        uint16_t next;
        random_get(&next, sizeof(next));
        next = PORT_FIRST + next % (PORT_LAST - PORT_FIRST + 1);

        for (int i = 0; i <= PORT_LAST - PORT_FIRST; ++i) {
            uint16_t p = htons(next);
            next = next == PORT_LAST ? PORT_FIRST : next + 1;

            if (!port_used(sock->proto, addr, p)) {
                port = p;
                break;
            }
        }

        if (!port)
            return -EADDRINUSE;
    } else if (port_used(sock->proto, addr, port)) {
        return -EADDRINUSE;
    }

    sock->laddr = addr;
    sock->lport = port;
    sock->next = bound;
    bound = sock;

    return 0;
}

/** Takes a socket off the list of bound sockets. */
static void sock_unbind(struct inet_sock *sock)
{
    for (struct inet_sock **p = &bound; *p; p = &(*p)->next) {
        if (*p == sock) {
            *p = sock->next;
            break;
        }
    }

    sock->lport = 0;
}

/**
 * Finds the socket that a received datagram is for: a socket connected to
 * its sender first, or else one that isn't connected. A socket bound to
 * another interface doesn't count. For ping sockets, the remote port
 * doesn't count.
 *
 * @param netif        the interface it came in on
 * @param proto        `IP_PROTO_UDP` or `IP_PROTO_ICMP`
 * @param local        the address it was sent to
 * @param port         the port it was sent to (a ping's identifier)
 * @param remote       the sender's address
 * @param remote_port  the sender's port
 * @return the socket, or NULL
 */
struct inet_sock *inet_sock_find(struct netif *netif, uint8_t proto, uint32_t local,
                                 uint16_t port, uint32_t remote, uint16_t remote_port)
{
    struct inet_sock *any = NULL;

    for (struct inet_sock *s = bound; s; s = s->next) {
        if (s->proto != proto || s->lport != port || (s->laddr && s->laddr != local) ||
            (s->netif && s->netif != netif))
            continue;

        if (!s->connected) {
            if (!any)
                any = s;
            continue;
        }

        if (s->raddr == remote && (proto == IP_PROTO_ICMP || s->rport == remote_port))
            return s;
    }

    return any;
}

/**
 * Queues a received datagram on its socket, and wakes up its readers. The
 * datagram is dropped if the socket's queue is full or reading is shut
 * down. The netbuf is the socket's from now on.
 *
 * @param sock  the socket
 * @param nb    the datagram's data, with `src_ip` and `src_port` set
 */
void inet_sock_deliver(struct inet_sock *sock, struct netbuf *nb)
{
    if (sock->shut_rd || sock->recv_bytes + NETBUF_SIZE > RECV_MAX) {
        netbuf_free(nb);
        return;
    }

    nb->next = NULL;

    if (sock->recv_tail)
        sock->recv_tail->next = nb;
    else
        sock->recv_head = nb;

    sock->recv_tail = nb;
    sock->recv_bytes += NETBUF_SIZE;

    waitq_wakeup(&sock->recv_wait);
}

/**
 * Gives a socket an error (from ICMP), which its next call reports.
 *
 * @param sock  the socket
 * @param err   the error (a positive errno)
 */
void inet_sock_error(struct inet_sock *sock, int err)
{
    sock->error = err;
    waitq_wakeup(&sock->recv_wait);
}

/*
 * Operations
 */

/**
 * Makes an `AF_INET` socket: a TCP, UDP or ping socket.
 *
 * @param file      the open file that becomes the socket
 * @param domain    `AF_INET`
 * @param type      `SOCK_STREAM` or `SOCK_DGRAM`
 * @param protocol  0, `IPPROTO_TCP`, `IPPROTO_UDP` or `IPPROTO_ICMP`
 * @return 0, `-EPROTONOSUPPORT`, or `-ENOMEM`
 */
int socket_inet_create(struct file *file, int domain, int type, int protocol)
{
    uint8_t proto;

    if (type == SOCK_STREAM && (!protocol || protocol == IPPROTO_TCP))
        return socket_tcp_create(file, domain, type, protocol);

    if (type != SOCK_DGRAM)
        return -EPROTONOSUPPORT;

    switch (protocol) {
        case 0:
        case IPPROTO_UDP:
            proto = IP_PROTO_UDP;
            break;
        case IPPROTO_ICMP:
            proto = IP_PROTO_ICMP;
            break;
        default:
            return -EPROTONOSUPPORT;
    }

    struct socket *socket = kmalloc(sizeof(struct socket), &M_SOCKET, M_ZERO);
    struct inet_sock *sock = kmalloc(sizeof(struct inet_sock), &M_INET_SOCK, M_ZERO);

    if (!socket || !sock) {
        kfree(socket);
        kfree(sock);
        return -ENOMEM;
    }

    sock->proto = proto;

    socket->domain   = domain;
    socket->type     = type;
    socket->protocol = protocol;
    socket->ops      = &socket_inet_ops;
    socket->p        = sock;

    file->flags |= FILE_SOCKET;
    file->socket = socket;
    return 0;
}

/**
 * Reads an `AF_INET` address.
 *
 * @param addr  the address, in the kernel's memory
 * @param len   its size
 * @param ip    set to its IPv4 address
 * @param port  set to its port
 * @return 0, `-EINVAL` if it's too short, or `-EAFNOSUPPORT`
 */
int inet_sockaddr_get(const struct sockaddr *addr, socklen_t len, uint32_t *ip, uint16_t *port)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *) addr;

    if (len < sizeof(*sin))
        return -EINVAL;

    if (sin->sin_family != AF_INET)
        return -EAFNOSUPPORT;

    *ip = sin->sin_addr.s_addr;
    *port = sin->sin_port;
    return 0;
}

/**
 * Writes an `AF_INET` address, cut to `*len` bytes if it doesn't fit.
 *
 * @param addr  where to write it
 * @param len   its room, set to the address's full size
 * @param ip    the IPv4 address
 * @param port  the port
 */
void inet_sockaddr_put(struct sockaddr *addr, socklen_t *len, uint32_t ip, uint16_t port)
{
    struct sockaddr_in sin = {
        .sin_family = AF_INET,
        .sin_port = port,
        .sin_addr = {ip},
    };

    memcpy(addr, &sin, MIN(*len, sizeof(sin)));
    *len = sizeof(sin);
}

/** Binds a socket to an address that is ours, any address, or the
 * broadcast address. Ports below 1024 are root's. */
static int socket_inet_bind(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    struct inet_sock *sock = file->socket->p;
    uint32_t ip;
    uint16_t port;
    int err;

    if ((err = inet_sockaddr_get(addr, len, &ip, &port)))
        return err;

    if (sock->lport)
        return -EINVAL;

    /* any address, one of ours, or the broadcast address (to receive only
     * what is sent to it, as a DHCP client does) */
    if (ip && ip != 0xffffffff && !ipv4_is_local(ip))
        return -EADDRNOTAVAIL;

    /* only root may bind to the ports below 1024 */
    if (sock->proto == IP_PROTO_UDP && ntohs(port) && ntohs(port) < 1024 && curproc->uid)
        return -EACCES;

    return sock_bind(sock, ip, port);
}

/** Connects a datagram socket: its default destination, and the only
 * sender it receives from. `AF_UNSPEC` disconnects it. */
static int socket_inet_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    struct inet_sock *sock = file->socket->p;
    uint32_t ip;
    uint16_t port;
    int err;

    /* AF_UNSPEC: disconnect */
    if (len >= sizeof(sa_family_t) && addr->sa_family == AF_UNSPEC) {
        sock->connected = 0;
        sock->raddr = 0;
        sock->rport = 0;
        return 0;
    }

    if ((err = inet_sockaddr_get(addr, len, &ip, &port)))
        return err;

    if (sock->proto == IP_PROTO_UDP && !port)
        return -EINVAL;

    if (!sock->lport && (err = sock_bind(sock, 0, 0)))
        return err;

    sock->raddr = ip;
    sock->rport = port;
    sock->connected = 1;
    sock->error = 0;
    return 0;
}

/** Sends a datagram, whole: to `to`, or to the connected peer. A ping
 * socket's data must be an echo request; its identifier is the socket's.
 * Returns the datagram's size, or a negative errno. */
static ssize_t socket_inet_send(struct file *file, struct iobuf *io, int flags,
                                const struct sockaddr *to, socklen_t tolen)
{
    struct inet_sock *sock = file->socket->p;
    size_t len = io->resid;
    uint32_t ip;
    uint16_t port;
    int err;

    if (to) {
        if ((err = inet_sockaddr_get(to, tolen, &ip, &port)))
            return err;
    } else if (sock->connected) {
        ip = sock->raddr;
        port = sock->rport;
    } else {
        return -EDESTADDRREQ;
    }

    if (sock->error) {
        err = -sock->error;
        sock->error = 0;
        return err;
    }

    /* a socket bound to an interface sends through it, whatever the routes
     * say */
    struct netif *netif = sock->netif ? sock->netif : ipv4_route(ip);

    if (!netif)
        return -ENETUNREACH;

    if (!(netif->flags & NETIF_UP))
        return -ENETDOWN;

    if ((ip == 0xffffffff || ip == (netif->ip | ~netif->netmask)) && !sock->broadcast)
        return -EACCES;

    size_t header = sock->proto == IP_PROTO_UDP ? sizeof(struct udp_hdr) : 0;

    if (len + header + sizeof(struct ipv4_hdr) > netif->mtu)
        return -EMSGSIZE;

    if (!sock->lport && (err = sock_bind(sock, 0, 0)))
        return err;

    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return -ENOBUFS;

    /* send from the address it is bound to, if any (but not from the
     * broadcast address: from the interface's then, or 0.0.0.0 if it has
     * none) */
    uint32_t src = sock->laddr && sock->laddr != 0xffffffff ? sock->laddr :
                   ipv4_source(netif, ip);

    /* copy in the whole datagram, or nothing */
    void *data = netbuf_put(nb, len);

    if ((size_t) iobuf_in(io, data, len) != len) {
        netbuf_free(nb);
        return -EFAULT;
    }

    if (sock->proto == IP_PROTO_UDP) {
        struct udp_hdr *uh = netbuf_push(nb, sizeof(*uh));
        uh->src = sock->lport;
        uh->dst = port;
        uh->len = htons(nb->len);
        uh->checksum = 0;
        uh->checksum = inet_checksum_pseudo(src, ip, IP_PROTO_UDP, uh, nb->len);

        /* 0 means no checksum, so a checksum of 0 is sent as all ones */
        if (!uh->checksum)
            uh->checksum = 0xffff;
    } else {
        /* ping: only echo requests, with the socket's identifier */
        struct icmp_hdr *icmp = data;

        if (len < sizeof(*icmp) || icmp->type != ICMP_ECHO_REQUEST || icmp->code) {
            netbuf_free(nb);
            return -EINVAL;
        }

        icmp->id = sock->lport;
        icmp->checksum = 0;
        icmp->checksum = inet_checksum(icmp, len);
    }

    if ((err = ipv4_output(netif, src, ip, sock->proto, nb)) && err != -ENOBUFS)
        return err;

    return len;
}

/** Receives a datagram, waiting for one unless the socket doesn't block
 * or the receive timeout passes. A datagram longer than the buffer is cut;
 * the rest is lost. Returns its size, 0 once reading is shut down, or a
 * negative errno. */
static ssize_t socket_inet_recv(struct file *file, struct iobuf *io, int flags,
                                struct sockaddr *from, socklen_t *fromlen)
{
    struct inet_sock *sock = file->socket->p;
    uint64_t until = sock->rcvtimeo ? arch_rtime_ns() + sock->rcvtimeo : 0;

    while (!sock->recv_head) {
        if (sock->error) {
            int err = sock->error;
            sock->error = 0;
            return -err;
        }

        if (sock->shut_rd)
            return 0;

        if ((file->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT))
            return -EAGAIN;

        int r = sched_wait(&sock->recv_wait, until);

        if (r)
            return r < 0 ? r : -EAGAIN;
    }

    struct netbuf *nb = sock->recv_head;

    /* if copying out faults, the datagram stays queued */
    ssize_t n = iobuf_out(io, nb->data, nb->len);

    if (n < 0)
        return n;

    if (from && fromlen)
        inet_sockaddr_put(from, fromlen, nb->src_ip, nb->src_port);

    /* the datagram is gone, even the part that didn't fit */
    if (!(flags & MSG_PEEK)) {
        sock->recv_head = nb->next;

        if (!sock->recv_head)
            sock->recv_tail = NULL;

        sock->recv_bytes -= NETBUF_SIZE;
        netbuf_free(nb);
    }

    return n;
}

/** Says whether a read wouldn't wait: a datagram is queued, or there is an
 * error to report, or reading is shut down. */
static int socket_inet_can_read(struct file *file, size_t len)
{
    struct inet_sock *sock = file->socket->p;

    return sock->recv_head ? (int) sock->recv_head->len + 1 : sock->error || sock->shut_rd;
}

/** Says that a write never waits. */
static int socket_inet_can_write(struct file *file, size_t len)
{
    return 1;
}

/** Returns the wait queue that the socket's readers sleep on; writers
 * never wait. */
static struct waitq *socket_inet_wait_queue(struct file *file, int write)
{
    struct inet_sock *sock = file->socket->p;
    return write ? NULL : &sock->recv_wait;
}

/** Gets the socket's local address, or its peer's (`-ENOTCONN` if it isn't
 * connected). */
static int socket_inet_getname(struct file *file, struct sockaddr *addr, socklen_t *len, int peer)
{
    struct inet_sock *sock = file->socket->p;

    if (peer) {
        if (!sock->connected)
            return -ENOTCONN;

        inet_sockaddr_put(addr, len, sock->raddr, sock->rport);
    } else {
        inet_sockaddr_put(addr, len, sock->laddr, sock->lport);
    }

    return 0;
}

/** Reads a socket option: `SO_ERROR`, `SO_BROADCAST`, `SO_BINDTODEVICE` or
 * `SO_RCVTIMEO`. */
static int socket_inet_getsockopt(struct file *file, int level, int name, void *val, socklen_t *len)
{
    struct inet_sock *sock = file->socket->p;

    if (level != SOL_SOCKET)
        return -ENOPROTOOPT;

    switch (name) {
        case SO_ERROR:
        case SO_BROADCAST:
            if (*len < sizeof(int))
                return -EINVAL;

            if (name == SO_ERROR) {
                *(int *) val = sock->error;
                sock->error = 0;
            } else {
                *(int *) val = sock->broadcast;
            }

            *len = sizeof(int);
            return 0;

        case SO_BINDTODEVICE: {
            /* the interface's name, or "" if none */
            const char *name = sock->netif ? sock->netif->name : "";
            size_t n = strlen(name) + 1;

            if (*len < n)
                return -EINVAL;

            memcpy(val, name, n);
            *len = n;
            return 0;
        }

        case SO_RCVTIMEO: {
            if (*len < sizeof(struct timeval))
                return -EINVAL;

            struct timeval *tv = val;
            tv->tv_sec  = sock->rcvtimeo / 1000000000ULL;
            tv->tv_usec = sock->rcvtimeo % 1000000000ULL / 1000;
            *len = sizeof(struct timeval);
            return 0;
        }
    }

    return -ENOPROTOOPT;
}

/** Sets a socket option: `SO_BROADCAST`, `SO_BINDTODEVICE` or
 * `SO_RCVTIMEO`. */
static int socket_inet_setsockopt(struct file *file, int level, int name, const void *val, socklen_t len)
{
    struct inet_sock *sock = file->socket->p;

    if (level != SOL_SOCKET)
        return -ENOPROTOOPT;

    switch (name) {
        case SO_BROADCAST:
            if (len < sizeof(int))
                return -EINVAL;

            sock->broadcast = !!*(const int *) val;
            return 0;

        case SO_BINDTODEVICE: {
            /* an interface's name (in a buffer that may be longer, such as
             * a struct ifreq), or none if empty; only root may set it, as
             * on Linux */
            char name[17] = {0};

            if (curproc->uid)
                return -EPERM;

            memcpy(name, val, MIN(len, (socklen_t) 16));

            if (!name[0]) {
                sock->netif = NULL;
                return 0;
            }

            struct netif *netif = netif_find(name);

            if (!netif)
                return -ENODEV;

            sock->netif = netif;
            return 0;
        }

        case SO_RCVTIMEO: {
            if (len < sizeof(struct timeval))
                return -EINVAL;

            const struct timeval *tv = val;

            if (tv->tv_usec < 0 || tv->tv_usec >= 1000000)
                return -EDOM;

            sock->rcvtimeo = (uint64_t) tv->tv_sec * 1000000000ULL + tv->tv_usec * 1000ULL;
            return 0;
        }
    }

    return -ENOPROTOOPT;
}

/** Handles `FIONREAD` (the size of the next datagram) and the interface
 * requests. */
static int socket_inet_ioctl(struct file *file, int request, void *argp)
{
    struct inet_sock *sock = file->socket->p;

    if (request == FIONREAD) {
        int n = sock->recv_head ? (int) sock->recv_head->len : 0;
        return copyout(argp, &n, sizeof(n));
    }

    return inet_ifioctl(request, argp);
}

/** Shuts down reading: the readers wake up and see the end. Shutting down
 * writing does nothing for a datagram socket. */
static int socket_inet_shut(struct file *file, int how)
{
    struct inet_sock *sock = file->socket->p;

    if (how & SHUT_RD) {
        sock->shut_rd = 1;
        waitq_wakeup(&sock->recv_wait);
    }

    return 0;
}

/** Frees the socket when its last descriptor is closed: it is unbound, and
 * its queued datagrams are freed. */
static int socket_inet_shutdown(struct file *file, int how)
{
    struct inet_sock *sock = file->socket->p;

    if (sock->lport)
        sock_unbind(sock);

    while (sock->recv_head) {
        struct netbuf *nb = sock->recv_head;
        sock->recv_head = nb->next;
        netbuf_free(nb);
    }

    kfree(sock);
    kfree(file->socket);
    file->socket = NULL;
    return 0;
}

static struct sock_ops socket_inet_ops = {
    .bind       = socket_inet_bind,
    .connect    = socket_inet_connect,
    .recv       = socket_inet_recv,
    .send       = socket_inet_send,
    .can_read   = socket_inet_can_read,
    .can_write  = socket_inet_can_write,
    .wait_queue = socket_inet_wait_queue,
    .getname    = socket_inet_getname,
    .getsockopt = socket_inet_getsockopt,
    .setsockopt = socket_inet_setsockopt,
    .ioctl      = socket_inet_ioctl,
    .shut       = socket_inet_shut,
    .shutdown   = socket_inet_shutdown,
};
