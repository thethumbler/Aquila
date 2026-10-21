/*
 * AF_INET sockets: UDP (SOCK_DGRAM), and ping (SOCK_DGRAM, IPPROTO_ICMP:
 * echo requests sent, the replies to them received, by anyone)
 *
 * A socket bound to a port (for a ping socket, the identifier of its
 * echoes) is on the list of the bound ones, which the protocols look
 * datagrams' sockets up in. Received datagrams wait in the socket's queue
 * (RECV_MAX bytes of them at most, the others are dropped), with where
 * they came from.
 */

#include <core/system.h>
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

#define RECV_MAX        65536

#define PORT_FIRST      49152   /* the ephemeral ports */
#define PORT_LAST       65535

struct inet_sock {
    uint8_t proto;              /* IP_PROTO_UDP, IP_PROTO_ICMP */

    /* in the network's byte order; not bound while lport is 0 */
    uint32_t laddr;
    uint16_t lport;
    uint32_t raddr;
    uint16_t rport;
    int connected;

    struct netbuf *recv_head, *recv_tail;
    size_t recv_bytes;
    struct queue recv_wait;

    struct netif *netif;        /* SO_BINDTODEVICE: only through it */

    int error;                  /* to report (SO_ERROR, the next call) */
    uint64_t rcvtimeo;          /* ns, 0: none */
    int broadcast;              /* SO_BROADCAST */
    int shut_rd;

    struct inet_sock *next;     /* bound */
};

static struct inet_sock *bound;

static struct sock_ops socket_inet_ops;

/*
 * The lists
 */

static int port_used(uint8_t proto, uint32_t addr, uint16_t port)
{
    for (struct inet_sock *s = bound; s; s = s->next)
        if (s->proto == proto && s->lport == port &&
            (!s->laddr || !addr || s->laddr == addr))
            return 1;

    return 0;
}

static int sock_bind(struct inet_sock *sock, uint32_t addr, uint16_t port)
{
    if (!port) {
        static uint16_t next = PORT_FIRST;

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

/* the socket a datagram from (remote, remote_port) to (local, port),
 * received on `netif', is for: a connected one to that remote first, else
 * one that isn't connected (the remote port doesn't count for ping); not
 * one bound to another interface */
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

/* a datagram for it (src_ip, src_port set), the netbuf is the socket's */
void inet_sock_deliver(struct inet_sock *sock, struct netbuf *nb)
{
    if (sock->shut_rd || sock->recv_bytes + nb->len > RECV_MAX) {
        netbuf_free(nb);
        return;
    }

    nb->next = NULL;

    if (sock->recv_tail)
        sock->recv_tail->next = nb;
    else
        sock->recv_head = nb;

    sock->recv_tail = nb;
    sock->recv_bytes += nb->len;

    thread_queue_wakeup(&sock->recv_wait);
}

/* an error for it (ICMP's), the next call reports it */
void inet_sock_error(struct inet_sock *sock, int err)
{
    sock->error = err;
    thread_queue_wakeup(&sock->recv_wait);
}

/*
 * Operations
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
    socket->ref      = 1;

    file->flags |= FILE_SOCKET;
    file->socket = socket;
    return 0;
}

/* an AF_INET address of `len' bytes at `addr' */
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

/* one of ours (or any) */
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

    /* any, one of ours, or the broadcast address (to receive what is sent
     * to it only, as a DHCP client does) */
    if (ip && ip != 0xffffffff && !ipv4_is_local(ip))
        return -EADDRNOTAVAIL;

    /* the ports below 1024 are root's */
    if (sock->proto == IP_PROTO_UDP && ntohs(port) && ntohs(port) < 1024 && curproc->uid)
        return -EACCES;

    return sock_bind(sock, ip, port);
}

static int socket_inet_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    struct inet_sock *sock = file->socket->p;
    uint32_t ip;
    uint16_t port;
    int err;

    /* AF_UNSPEC: not connected anymore */
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

static ssize_t socket_inet_send(struct file *file, void *buf, size_t len, int flags,
                                const struct sockaddr *to, socklen_t tolen)
{
    struct inet_sock *sock = file->socket->p;
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

    /* bound to an interface: through it, whatever the routes say */
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

    /* from the address it's bound to, if it is (not the broadcast
     * address: from the interface's then, 0.0.0.0 if it has none) */
    uint32_t src = sock->laddr && sock->laddr != 0xffffffff ? sock->laddr :
                   ipv4_source(netif, ip);

    if (sock->proto == IP_PROTO_UDP) {
        memcpy(netbuf_put(nb, len), buf, len);

        struct udp_hdr *uh = netbuf_push(nb, sizeof(*uh));
        uh->src = sock->lport;
        uh->dst = port;
        uh->len = htons(nb->len);
        uh->checksum = 0;
        uh->checksum = inet_checksum_pseudo(src, ip, IP_PROTO_UDP, uh, nb->len);

        /* 0 is no checksum, all ones is it too */
        if (!uh->checksum)
            uh->checksum = 0xffff;
    } else {
        /* ping: echo requests, with the socket's identifier */
        struct icmp_hdr *icmp = (struct icmp_hdr *) buf;

        if (len < sizeof(*icmp) || icmp->type != ICMP_ECHO_REQUEST || icmp->code) {
            netbuf_free(nb);
            return -EINVAL;
        }

        icmp = netbuf_put(nb, len);
        memcpy(icmp, buf, len);
        icmp->id = sock->lport;
        icmp->checksum = 0;
        icmp->checksum = inet_checksum(icmp, len);
    }

    if ((err = ipv4_output(netif, src, ip, sock->proto, nb)) && err != -ENOBUFS)
        return err;

    return len;
}

static ssize_t socket_inet_recv(struct file *file, void *buf, size_t len, int flags,
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

        if (sched_wait(&sock->recv_wait, until))
            return -EAGAIN;
    }

    struct netbuf *nb = sock->recv_head;
    size_t n = MIN(len, nb->len);

    memcpy(buf, nb->data, n);

    if (from && fromlen)
        inet_sockaddr_put(from, fromlen, nb->src_ip, nb->src_port);

    /* the rest of a datagram longer than the buffer goes too */
    if (!(flags & MSG_PEEK)) {
        sock->recv_head = nb->next;

        if (!sock->recv_head)
            sock->recv_tail = NULL;

        sock->recv_bytes -= nb->len;
        netbuf_free(nb);
    }

    return n;
}

static int socket_inet_can_read(struct file *file, size_t len)
{
    struct inet_sock *sock = file->socket->p;

    /* a datagram, an error to report, or the end */
    return sock->recv_head ? (int) sock->recv_head->len + 1 : sock->error || sock->shut_rd;
}

static int socket_inet_can_write(struct file *file, size_t len)
{
    return 1;
}

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
            /* its name, "" if none */
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
            /* an interface's name (in a buffer that may be longer, as a
             * struct ifreq), none if empty; root's, as Linux has it */
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

static int socket_inet_ioctl(struct file *file, int request, void *argp)
{
    struct inet_sock *sock = file->socket->p;

    /* the size of the next datagram */
    if (request == FIONREAD) {
        if (uaccess_check(argp, sizeof(int)))
            return -EFAULT;

        *(int *) argp = sock->recv_head ? (int) sock->recv_head->len : 0;
        return 0;
    }

    return inet_ifioctl(request, argp);
}

static int socket_inet_shut(struct file *file, int how)
{
    struct inet_sock *sock = file->socket->p;

    if (how & SHUT_RD) {
        sock->shut_rd = 1;
        thread_queue_wakeup(&sock->recv_wait);
    }

    return 0;
}

/* the last descriptor is closed */
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
    .getname    = socket_inet_getname,
    .getsockopt = socket_inet_getsockopt,
    .setsockopt = socket_inet_setsockopt,
    .ioctl      = socket_inet_ioctl,
    .shut       = socket_inet_shut,
    .shutdown   = socket_inet_shutdown,
};
