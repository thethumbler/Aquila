/** \file
 * TCP connections (`struct tcp_pcb`) and their sockets (`SOCK_STREAM`).
 *
 * Every connection is on one list, where the ports in use and the
 * listeners are found. Once it has both ends' addresses and ports, it is
 * also in a hashmap by them, where received segments find their
 * connection. A socket has its connection. When the socket is closed, the
 * connection stays (detached) until it is done: its data and FIN sent,
 * and `TIME_WAIT` over. A connection that a listener made has no socket
 * until `accept()` takes it.
 *
 * Received segments and the timers are handled in the network's thread,
 * and sending in the callers' system calls. Copying data to and from user
 * memory may fault and sleep, while the network's thread changes the
 * rings, so the code is careful about which end of a ring it touches.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <dev/random.h>
#include <ds/hashmap.h>
#include <fs/vfs.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <net/netif.h>
#include <net/socket.h>
#include <net/tcp.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/uaccess.h>

#include <aquila/errno.h>
#include <aquila/fcntl.h>
#include <aquila/in.h>
#include <aquila/ioctl.h>
#include <aquila/tcp.h>

MALLOC_DECLARE(M_SOCKET);
MALLOC_DEFINE(M_TCP_PCB, "tcp pcb", "TCP connection");

#define PORT_FIRST      49152   /* the ephemeral ports' range */
#define PORT_LAST       65535

#define MSL             30000000000ULL      /* 30 s: TIME_WAIT lasts 2 MSL */
#define FIN_WAIT_2_TIME 60000000000ULL      /* how long a closed socket waits for the peer's FIN */
#define RTO_INITIAL     1000000000ULL

#define KEEP_IDLE       7200000000000ULL    /* keepalive: after 2 h of silence, */
#define KEEP_INTERVAL   75000000000ULL      /* a probe every 75 s, */
#define KEEP_PROBES     9                   /* and ETIMEDOUT after 9 unanswered */

#define COPY_CHUNK      512

struct tcp_pcb *tcp_pcbs;
static struct hashmap *conns;
static uint8_t iss_secret[16];

static struct sock_ops socket_tcp_ops;

/*
 * The connections
 */

/** What identifies a connection: both ends' addresses and ports. */
struct tcp_key {
    uint32_t laddr, raddr;
    uint16_t lport, rport;
};

/** Returns a connection key's hash. */
static hash_t key_hash(const struct tcp_key *key)
{
    return hashmap_digest(key, sizeof(*key));
}

/** Returns whether a connection has the key `k`. */
static int key_eq(void *entry, void *k)
{
    struct tcp_pcb *pcb = entry;
    struct tcp_key *key = k;

    return pcb->laddr == key->laddr && pcb->raddr == key->raddr &&
           pcb->lport == key->lport && pcb->rport == key->rport;
}

/**
 * Finds a connection by both ends' addresses and ports.
 *
 * @param laddr  the local address
 * @param lport  the local port
 * @param raddr  the remote address
 * @param rport  the remote port
 * @return the connection, or NULL
 */
struct tcp_pcb *tcp_lookup(uint32_t laddr, uint16_t lport, uint32_t raddr, uint16_t rport)
{
    struct tcp_key key = {laddr, raddr, lport, rport};
    struct hashmap_node *node = conns ? hashmap_lookup(conns, key_hash(&key), &key) : NULL;

    return node ? node->entry : NULL;
}

/**
 * Adds a connection to the hashmap of connections, by its addresses and
 * ports, which it must have now.
 *
 * @param pcb  the connection
 * @return 0, `-EADDRINUSE` if another connection has the same ones, or
 *         `-ENOMEM`
 */
int tcp_insert(struct tcp_pcb *pcb)
{
    struct tcp_key key = {pcb->laddr, pcb->raddr, pcb->lport, pcb->rport};
    hash_t hash = key_hash(&key);

    if (!conns && !(conns = hashmap_new(0, key_eq)))
        return -ENOMEM;

    if (hashmap_lookup(conns, hash, &key))
        return -EADDRINUSE;

    if (hashmap_insert(conns, hash, pcb))
        return -ENOMEM;

    pcb->hnode = hashmap_lookup(conns, hash, &key);
    return 0;
}

/**
 * Finds the listener for a local address and port: one bound to that
 * address, or else one bound to any address.
 *
 * @param laddr  the local address
 * @param lport  the local port
 * @return the listener, or NULL
 */
struct tcp_pcb *tcp_listener(uint32_t laddr, uint16_t lport)
{
    struct tcp_pcb *any = NULL;

    for (struct tcp_pcb *pcb = tcp_pcbs; pcb; pcb = pcb->next) {
        if (pcb->state != TCP_LISTEN || pcb->lport != lport)
            continue;

        if (pcb->laddr == laddr)
            return pcb;

        if (!pcb->laddr)
            any = pcb;
    }

    return any;
}

/**
 * Makes a connection, `CLOSED`, with its send and receive buffers, on the
 * list of connections.
 *
 * @return the connection, or NULL if there was no memory
 */
struct tcp_pcb *tcp_pcb_new(void)
{
    struct tcp_pcb *pcb = kmalloc(sizeof(struct tcp_pcb), &M_TCP_PCB, M_ZERO);

    if (!pcb)
        return NULL;

    pcb->sndbuf = ringbuf_new(TCP_BUFSIZE);
    pcb->rcvbuf = ringbuf_new(TCP_BUFSIZE);

    if (!pcb->sndbuf || !pcb->rcvbuf) {
        ringbuf_free(pcb->sndbuf);
        ringbuf_free(pcb->rcvbuf);
        kfree(pcb);
        return NULL;
    }

    pcb->state    = TCP_CLOSED;
    pcb->mss      = TCP_MSS_DEFAULT;
    pcb->rto      = RTO_INITIAL;
    pcb->ssthresh = 0xffffffff;

    pcb->next = tcp_pcbs;
    tcp_pcbs = pcb;

    return pcb;
}

/**
 * Frees a connection: its timers stop, it leaves the lists, and its
 * buffers and waiting segments are freed.
 *
 * @param pcb  the connection
 */
void tcp_pcb_free(struct tcp_pcb *pcb)
{
    callout_stop(&pcb->rexmt);
    callout_stop(&pcb->dack);
    callout_stop(&pcb->timer2);

    if (pcb->hnode)
        hashmap_node_remove(conns, pcb->hnode);

    for (struct tcp_pcb **p = &tcp_pcbs; *p; p = &(*p)->next) {
        if (*p == pcb) {
            *p = pcb->next;
            break;
        }
    }

    while (pcb->ooo) {
        struct netbuf *nb = pcb->ooo;
        pcb->ooo = nb->next;
        netbuf_free(nb);
    }

    ringbuf_free(pcb->sndbuf);
    ringbuf_free(pcb->rcvbuf);
    kfree(pcb);
}

/**
 * Chooses a connection's initial sequence number (RFC 6528): a clock that
 * ticks every 4 us, plus a hash of the connection's addresses and ports
 * with a secret, so that it can't be guessed.
 *
 * @param pcb  the connection, with its addresses and ports
 * @return the initial sequence number
 */
uint32_t tcp_iss(struct tcp_pcb *pcb)
{
    static int keyed;

    if (!keyed) {
        random_get(iss_secret, sizeof(iss_secret));
        keyed = 1;
    }

    struct {
        struct tcp_key key;
        uint8_t secret[16];
    } in;

    in.key = (struct tcp_key) {pcb->laddr, pcb->raddr, pcb->lport, pcb->rport};
    memcpy(in.secret, iss_secret, sizeof(in.secret));

    return (uint32_t) (arch_rtime_ns() / 4000) + (uint32_t) hashmap_digest(&in, sizeof(in));
}

/**
 * Wakes up the threads waiting on a connection (in `accept()`,
 * `connect()`, `recv()` or `send()`).
 *
 * @param pcb  the connection
 */
void tcp_wakeup(struct tcp_pcb *pcb)
{
    waitq_wakeup(&pcb->wait);
}

/** Takes a connection off its listener's queue, when it is done or
 * `accept()` took it. */
static void unlink_parent(struct tcp_pcb *pcb)
{
    struct tcp_pcb *l = pcb->parent;

    if (!l)
        return;

    for (struct tcp_pcb **p = &l->accept_head; *p; p = &(*p)->accept_next) {
        if (*p == pcb) {
            *p = pcb->accept_next;
            if (l->accept_tail == pcb) {
                l->accept_tail = NULL;
                for (struct tcp_pcb *q = l->accept_head; q; q = q->accept_next)
                    l->accept_tail = q;
            }
            break;
        }
    }

    pcb->accept_next = NULL;
    pcb->parent = NULL;
    l->qlen--;
}

/**
 * Moves a connection to `CLOSED`: it leaves the hashmap, its timers stop,
 * and its waiters wake up. It is freed if nothing holds it anymore;
 * otherwise its socket keeps it.
 *
 * @param pcb  the connection
 */
void tcp_closed(struct tcp_pcb *pcb)
{
    pcb->state = TCP_CLOSED;

    if (pcb->hnode) {
        hashmap_node_remove(conns, pcb->hnode);
        pcb->hnode = NULL;
    }

    callout_stop(&pcb->rexmt);
    callout_stop(&pcb->dack);
    callout_stop(&pcb->timer2);
    tcp_wakeup(pcb);

    if (pcb->parent) {
        unlink_parent(pcb);
        tcp_pcb_free(pcb);
    } else if (pcb->detached) {
        tcp_pcb_free(pcb);
    }
}

/**
 * Drops a connection that is lost, giving its socket an error.
 *
 * @param pcb  the connection
 * @param err  the error for the socket (a positive errno), or 0
 */
void tcp_drop(struct tcp_pcb *pcb, int err)
{
    pcb->error = err;
    tcp_closed(pcb);
}

/**
 * Moves a connection to `ESTABLISHED`: its congestion window starts, and a
 * listener's connection is queued for `accept()`.
 *
 * @param pcb  the connection
 */
void tcp_established(struct tcp_pcb *pcb)
{
    pcb->state = TCP_ESTABLISHED;
    pcb->connected = 1;

    /* the initial congestion window (RFC 5681) */
    pcb->cwnd = MIN(4U * pcb->mss, MAX(2U * pcb->mss, 4380U));
    pcb->snd_nxt = pcb->snd_max = pcb->snd_una;

    tcp_keepalive_reset(pcb);

    /* a listener's connection waits for accept() */
    struct tcp_pcb *l = pcb->parent;

    if (l) {
        if (l->accept_tail)
            l->accept_tail->accept_next = pcb;
        else
            l->accept_head = pcb;

        l->accept_tail = pcb;
        tcp_wakeup(l);
    }

    tcp_wakeup(pcb);
}

/** Closes a connection when its `TIME_WAIT` or `FIN_WAIT_2` time is over. */
static void timer2_closed(void *arg)
{
    tcp_closed(arg);
}

/**
 * Moves a connection to `TIME_WAIT`, which it leaves after 2 MSL.
 *
 * @param pcb  the connection
 */
void tcp_time_wait(struct tcp_pcb *pcb)
{
    pcb->state = TCP_TIME_WAIT;
    callout_stop(&pcb->rexmt);
    net_callout_set(&pcb->timer2, 2 * MSL, timer2_closed, pcb);
    tcp_wakeup(pcb);
}

/**
 * Handles our FIN being acknowledged (`FIN_WAIT_2`): if the socket is
 * closed, the connection waits for the peer's FIN only for a while.
 *
 * @param pcb  the connection
 */
void tcp_fin_wait_2(struct tcp_pcb *pcb)
{
    if (pcb->detached)
        net_callout_set(&pcb->timer2, FIN_WAIT_2_TIME, timer2_closed, pcb);
}

/** Sends a keepalive probe after a silence, or drops the connection
 * (`ETIMEDOUT`) once too many probes went unanswered. */
static void keepalive_timeout(void *arg)
{
    struct tcp_pcb *pcb = arg;

    if (pcb->state != TCP_ESTABLISHED || !pcb->keepalive)
        return;

    if (pcb->keep_probes++ >= KEEP_PROBES) {
        tcp_send_rst(pcb);
        tcp_drop(pcb, ETIMEDOUT);
        return;
    }

    tcp_send_keepalive(pcb);
    net_callout_set(&pcb->timer2, KEEP_INTERVAL, keepalive_timeout, pcb);
}

/**
 * Restarts the keepalive timer, when the peer was heard from.
 *
 * @param pcb  the connection
 */
void tcp_keepalive_reset(struct tcp_pcb *pcb)
{
    pcb->keep_probes = 0;

    if (pcb->keepalive && pcb->state == TCP_ESTABLISHED)
        net_callout_set(&pcb->timer2, KEEP_IDLE, keepalive_timeout, pcb);
}

/*
 * Ports
 */

/** Returns whether a local address and port are taken: by a listener, or by
 * any connection unless `reuseaddr` is set. */
static int port_used(uint32_t addr, uint16_t port, int reuseaddr)
{
    for (struct tcp_pcb *pcb = tcp_pcbs; pcb; pcb = pcb->next) {
        if (pcb->lport != port || (pcb->laddr && addr && pcb->laddr != addr))
            continue;

        if (pcb->state == TCP_LISTEN || !reuseaddr)
            return 1;
    }

    return 0;
}

/** Binds a connection to a local address and port; port 0 picks a free
 * ephemeral port. Returns 0 or `-EADDRINUSE`. */
static int bind_port(struct tcp_pcb *pcb, uint32_t addr, uint16_t port)
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

            if (!port_used(0, p, 0)) {
                port = p;
                break;
            }
        }

        if (!port)
            return -EADDRINUSE;
    } else if (port_used(addr, port, pcb->reuseaddr)) {
        return -EADDRINUSE;
    }

    pcb->laddr = addr;
    pcb->lport = port;
    return 0;
}

/*
 * The socket
 */

/**
 * Makes a TCP socket, with its connection, `CLOSED`.
 *
 * @param file      the open file that becomes the socket
 * @param domain    `AF_INET`
 * @param type      `SOCK_STREAM`
 * @param protocol  0 or `IPPROTO_TCP`
 * @return 0, or `-ENOMEM`
 */
int socket_tcp_create(struct file *file, int domain, int type, int protocol)
{
    struct socket *socket = kmalloc(sizeof(struct socket), &M_SOCKET, M_ZERO);
    struct tcp_pcb *pcb = tcp_pcb_new();

    if (!socket || !pcb) {
        kfree(socket);
        if (pcb)
            tcp_pcb_free(pcb);
        return -ENOMEM;
    }

    socket->domain   = domain;
    socket->type     = type;
    socket->protocol = protocol;
    socket->ops      = &socket_tcp_ops;
    socket->p        = pcb;

    file->flags |= FILE_SOCKET;
    file->socket = socket;
    return 0;
}

/** Returns whether a call shouldn't wait: the socket doesn't block, or the
 * call has `MSG_DONTWAIT`. */
static int nonblock(struct file *file, int flags)
{
    return (file->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
}

/** Returns the deadline for a timeout of `timeo` ns from now, or 0 for
 * none. */
static uint64_t deadline(uint64_t timeo)
{
    return timeo ? arch_rtime_ns() + timeo : 0;
}

/** Binds the socket to a local address (ours, or any) and port; ports
 * below 1024 are root's. */
static int socket_tcp_bind(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    struct tcp_pcb *pcb = file->socket->p;
    uint32_t ip;
    uint16_t port;
    int err;

    if ((err = inet_sockaddr_get(addr, len, &ip, &port)))
        return err;

    if (pcb->state != TCP_CLOSED || pcb->lport)
        return -EINVAL;

    if (ip && !ipv4_is_local(ip))
        return -EADDRNOTAVAIL;

    /* only root may bind to the ports below 1024 */
    if (ntohs(port) && ntohs(port) < 1024 && curproc->uid)
        return -EACCES;

    return bind_port(pcb, ip, port);
}

/** Makes the socket listen, binding it to an ephemeral port if it isn't
 * bound. */
static int socket_tcp_listen(struct file *file, int backlog)
{
    struct tcp_pcb *pcb = file->socket->p;
    int err;

    if (pcb->state != TCP_CLOSED && pcb->state != TCP_LISTEN)
        return -EINVAL;

    if (!pcb->lport && (err = bind_port(pcb, 0, 0)))
        return err;

    pcb->state = TCP_LISTEN;
    pcb->backlog = MIN(MAX(backlog, 1), SOMAXCONN);
    return 0;
}

/** Accepts an established connection, waiting for one unless the socket
 * doesn't block or the receive timeout passes. `conn` becomes its socket. */
static int socket_tcp_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len)
{
    struct tcp_pcb *pcb = file->socket->p;
    int err;

    if (pcb->state != TCP_LISTEN)
        return -EINVAL;

    uint64_t until = deadline(pcb->rcvtimeo);

    while (!pcb->accept_head) {
        if (nonblock(file, 0))
            return -EAGAIN;

        if ((err = sched_wait(&pcb->wait, until)))
            return err < 0 ? err : -EAGAIN;

        if (pcb->state != TCP_LISTEN)
            return -EINVAL;
    }

    struct socket *socket = kmalloc(sizeof(struct socket), &M_SOCKET, M_ZERO);

    if (!socket)
        return -ENOMEM;

    struct tcp_pcb *child = pcb->accept_head;
    unlink_parent(child);

    memcpy(socket, file->socket, sizeof(struct socket));
    socket->p   = child;

    conn->socket = socket;
    conn->flags  = FILE_SOCKET;
    conn->offset = 0;

    if (addr)
        inet_sockaddr_put((struct sockaddr *) addr, len, child->raddr, child->rport);

    return 0;
}

/** Connects to a peer: sends the SYN, and waits for the connection to be
 * established, unless the socket doesn't block (`-EINPROGRESS`). */
static int socket_tcp_connect(struct file *file, const struct sockaddr *addr, socklen_t len)
{
    struct tcp_pcb *pcb = file->socket->p;
    uint32_t ip;
    uint16_t port;
    int err;

    switch (pcb->state) {
        case TCP_CLOSED:
            if (pcb->connected)
                return -EISCONN;
            break;
        case TCP_SYN_SENT:
        case TCP_SYN_RECEIVED:
            /* a blocking connect() that a signal interrupted and that was
             * restarted waits for the same connection, as on Linux */
            if (nonblock(file, 0))
                return -EALREADY;
            goto wait;
        case TCP_LISTEN:
            return -EINVAL;
        default:
            return -EISCONN;
    }

    if ((err = inet_sockaddr_get(addr, len, &ip, &port)))
        return err;

    if (!ip || !port || ip == 0xffffffff)
        return -EADDRNOTAVAIL;

    struct netif *netif = ipv4_route(ip);

    if (!netif)
        return -ENETUNREACH;

    if (!pcb->lport && (err = bind_port(pcb, 0, 0)))
        return err;

    if (!pcb->laddr)
        pcb->laddr = ipv4_source(netif, ip);

    pcb->raddr = ip;
    pcb->rport = port;
    pcb->error = 0;

    if ((err = tcp_insert(pcb))) {
        pcb->raddr = pcb->rport = 0;
        return err;
    }

    pcb->iss = tcp_iss(pcb);
    pcb->snd_una = pcb->iss;
    pcb->state = TCP_SYN_SENT;
    pcb->retries = 0;
    tcp_send_syn(pcb);

    if (nonblock(file, 0))
        return -EINPROGRESS;

wait:
    /* if a signal ends the wait, the connection goes on being made
     * without us */
    while (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RECEIVED)
        if ((err = sched_wait(&pcb->wait, 0)) < 0)
            return err;

    if (pcb->connected && pcb->state != TCP_CLOSED)
        return 0;

    err = pcb->error ? pcb->error : ECONNREFUSED;
    pcb->error = 0;
    return -err;
}

/** Returns whether data may be sent in the connection's state. */
static int can_send(struct tcp_pcb *pcb)
{
    return (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) && !pcb->fin_queued;
}

/** Sends data: it goes into the send buffer, waiting for room unless the
 * socket doesn't block, and out as the windows allow. Returns how much
 * was taken, or a negative errno (`-EPIPE` once sending is over). */
static ssize_t socket_tcp_send(struct file *file, struct iobuf *io, int flags,
                               const struct sockaddr *to, socklen_t tolen)
{
    struct tcp_pcb *pcb = file->socket->p;
    int r;
    uint64_t until = deadline(pcb->sndtimeo);
    size_t sent = 0, len = io->resid;

    if (flags & MSG_OOB)
        return -EOPNOTSUPP;

    while (sent < len) {
        if (pcb->error) {
            int err = pcb->error;
            pcb->error = 0;
            return sent ? (ssize_t) sent : -err;
        }

        /* still connecting: wait until it is connected */
        if (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RECEIVED) {
            if (nonblock(file, flags))
                return sent ? (ssize_t) sent : -EAGAIN;

            if ((r = sched_wait(&pcb->wait, until)))
                return sent ? (ssize_t) sent : r < 0 ? r : -EAGAIN;

            continue;
        }

        if (!can_send(pcb))
            return sent ? (ssize_t) sent : pcb->connected ? -EPIPE : -ENOTCONN;

        size_t room = TCP_BUFSIZE - 1 - ringbuf_available(pcb->sndbuf);

        if (!room) {
            tcp_output(pcb);

            if (nonblock(file, flags))
                return sent ? (ssize_t) sent : -EAGAIN;

            if ((r = sched_wait(&pcb->wait, until)))
                return sent ? (ssize_t) sent : r < 0 ? r : -EAGAIN;

            continue;
        }

        /* copy straight into the ring: the network's thread doesn't look
         * past its tail */
        ssize_t n = ringbuf_write_iobuf(pcb->sndbuf, MIN(room, len - sent), io);

        if (n < 0) {
            if (!sent)
                return n;
            break;
        }

        sent += n;

        if (io->fault)
            break;
    }

    tcp_output(pcb);
    return sent;
}

/** Tells the peer that reading made room in the receive window, if it
 * opened enough (`tcp_rcv_window()`). */
static void window_update(struct tcp_pcb *pcb)
{
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_FIN_WAIT_1 &&
        pcb->state != TCP_FIN_WAIT_2)
        return;

    uint32_t cur = SEQ_GT(pcb->rcv_adv, pcb->rcv_nxt) ? pcb->rcv_adv - pcb->rcv_nxt : 0;

    if (tcp_rcv_window(pcb) > cur)
        tcp_send_ack(pcb);
}

/** Receives data, waiting for some unless the socket doesn't block or the
 * receive timeout passes (with `MSG_WAITALL`, until the buffer is full).
 * Returns how much was read, 0 at the end, or a negative errno. */
static ssize_t socket_tcp_recv(struct file *file, struct iobuf *io, int flags,
                               struct sockaddr *from, socklen_t *fromlen)
{
    struct tcp_pcb *pcb = file->socket->p;
    int r;
    uint64_t until = deadline(pcb->rcvtimeo);
    size_t got = 0, len = io->resid;
    char chunk[COPY_CHUNK];

    if (flags & MSG_OOB)
        return -EOPNOTSUPP;

    if (pcb->state == TCP_LISTEN)
        return -ENOTCONN;

    while (got < len) {
        size_t avail = ringbuf_available(pcb->rcvbuf);

        /* a peek reads further into the ring each time, through a chunk
         * on the stack */
        if ((flags & MSG_PEEK) && avail > got) {
            size_t n = ringbuf_read_noconsume(pcb->rcvbuf, got, MIN(len - got, sizeof(chunk)), chunk);
            ssize_t c = iobuf_out(io, chunk, n);

            if (c < 0)
                return got ? (ssize_t) got : c;

            got += c;

            if (io->fault)
                break;

            continue;
        }

        /* copy straight from the ring: the network's thread doesn't look
         * before its head */
        if (avail && !(flags & MSG_PEEK)) {
            ssize_t n = ringbuf_read_iobuf(pcb->rcvbuf, len - got, io);

            if (n < 0)
                return got ? (ssize_t) got : n;

            got += n;
            window_update(pcb);

            if (io->fault)
                break;

            continue;
        }

        /* some data was read, and we don't wait for all of it */
        if (got && (!(flags & MSG_WAITALL) || (flags & MSG_PEEK)))
            break;

        if (pcb->error) {
            int err = pcb->error;
            pcb->error = 0;
            return got ? (ssize_t) got : -err;
        }

        /* the end of the data */
        if (pcb->fin_received || pcb->shut_rd || (pcb->state == TCP_CLOSED && pcb->connected))
            break;

        if (pcb->state == TCP_CLOSED)
            return -ENOTCONN;

        if (nonblock(file, flags))
            return got ? (ssize_t) got : -EAGAIN;

        if ((r = sched_wait(&pcb->wait, until)))
            return got ? (ssize_t) got : r < 0 ? r : -EAGAIN;
    }

    if (from && fromlen)
        inet_sockaddr_put(from, fromlen, pcb->raddr, pcb->rport);

    return got;
}

/** Says whether a read (or an accept) wouldn't wait: there is data, a
 * connection to accept, the end, or an error. */
static int socket_tcp_can_read(struct file *file, size_t len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->state == TCP_LISTEN)
        return pcb->accept_head != NULL;

    return ringbuf_available(pcb->rcvbuf) || pcb->fin_received || pcb->error || pcb->shut_rd ||
           (pcb->state == TCP_CLOSED && pcb->connected);
}

/** Returns the connection's wait queue, which `accept()`, `connect()`,
 * `recv()` and `send()` all wait on. */
static struct waitq *socket_tcp_wait_queue(struct file *file, int write)
{
    struct tcp_pcb *pcb = file->socket->p;
    return &pcb->wait;
}

/** Says whether a write wouldn't wait: there is room in the send buffer,
 * or an error or the end of sending to report. */
static int socket_tcp_can_write(struct file *file, size_t len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->error || (pcb->connected && !can_send(pcb)))
        return 1;

    return can_send(pcb) && ringbuf_available(pcb->sndbuf) < TCP_BUFSIZE - 1;
}

/** Gets the socket's local address, or its peer's (`-ENOTCONN` before the
 * peer is known). */
static int socket_tcp_getname(struct file *file, struct sockaddr *addr, socklen_t *len, int peer)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (peer) {
        if (pcb->state <= TCP_SYN_SENT)
            return -ENOTCONN;

        inet_sockaddr_put(addr, len, pcb->raddr, pcb->rport);
    } else {
        inet_sockaddr_put(addr, len, pcb->laddr, pcb->lport);
    }

    return 0;
}

/** Sets an option's value to the int `v`. */
static int int_get(void *val, socklen_t *len, int v)
{
    if (*len < sizeof(int))
        return -EINVAL;

    *(int *) val = v;
    *len = sizeof(int);
    return 0;
}

/** Sets an option's value to a timeout, as a `struct timeval`. */
static int timeo_get(void *val, socklen_t *len, uint64_t ns)
{
    if (*len < sizeof(struct timeval))
        return -EINVAL;

    struct timeval *tv = val;
    tv->tv_sec  = ns / 1000000000ULL;
    tv->tv_usec = ns % 1000000000ULL / 1000;
    *len = sizeof(struct timeval);
    return 0;
}

/** Reads a timeout from an option's value, a `struct timeval`, into
 * `*ns`. */
static int timeo_set(const void *val, socklen_t len, uint64_t *ns)
{
    if (len < sizeof(struct timeval))
        return -EINVAL;

    const struct timeval *tv = val;

    if (tv->tv_usec < 0 || tv->tv_usec >= 1000000)
        return -EDOM;

    *ns = (uint64_t) tv->tv_sec * 1000000000ULL + tv->tv_usec * 1000ULL;
    return 0;
}

/** Reads a TCP or socket option. */
static int socket_tcp_getsockopt(struct file *file, int level, int name, void *val, socklen_t *len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (level == IPPROTO_TCP) {
        switch (name) {
            case TCP_NODELAY: return int_get(val, len, pcb->nodelay);
            case TCP_MAXSEG:  return int_get(val, len, pcb->mss);
        }

        return -ENOPROTOOPT;
    }

    if (level != SOL_SOCKET)
        return -ENOPROTOOPT;

    switch (name) {
        case SO_ERROR: {
            int err = pcb->error;
            pcb->error = 0;
            return int_get(val, len, err);
        }
        case SO_KEEPALIVE:  return int_get(val, len, pcb->keepalive);
        case SO_REUSEADDR:  return int_get(val, len, pcb->reuseaddr);
        case SO_ACCEPTCONN: return int_get(val, len, pcb->state == TCP_LISTEN);
        case SO_SNDBUF:
        case SO_RCVBUF:     return int_get(val, len, TCP_BUFSIZE - 1);
        case SO_RCVTIMEO:   return timeo_get(val, len, pcb->rcvtimeo);
        case SO_SNDTIMEO:   return timeo_get(val, len, pcb->sndtimeo);
    }

    return -ENOPROTOOPT;
}

/** Sets a TCP or socket option. */
static int socket_tcp_setsockopt(struct file *file, int level, int name, const void *val, socklen_t len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (level == IPPROTO_TCP) {
        if (len < sizeof(int))
            return -EINVAL;

        switch (name) {
            case TCP_NODELAY:
                pcb->nodelay = !!*(const int *) val;
                tcp_output(pcb);
                return 0;
            case TCP_MAXSEG:
                /* accepted, without effect: the peer's MSS and the
                 * interface's MTU decide */
                return 0;
        }

        return -ENOPROTOOPT;
    }

    if (level != SOL_SOCKET)
        return -ENOPROTOOPT;

    switch (name) {
        case SO_KEEPALIVE:
            if (len < sizeof(int))
                return -EINVAL;

            pcb->keepalive = !!*(const int *) val;
            tcp_keepalive_reset(pcb);
            return 0;

        case SO_REUSEADDR:
            if (len < sizeof(int))
                return -EINVAL;

            pcb->reuseaddr = !!*(const int *) val;
            return 0;

        case SO_RCVTIMEO: return timeo_set(val, len, &pcb->rcvtimeo);
        case SO_SNDTIMEO: return timeo_set(val, len, &pcb->sndtimeo);
    }

    /* the socket layer accepts SO_SNDBUF, SO_RCVBUF and SO_LINGER */
    return -ENOPROTOOPT;
}

/** Handles `FIONREAD` (how much data can be read) and the interface
 * requests. */
static int socket_tcp_ioctl(struct file *file, int request, void *argp)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (request == FIONREAD) {
        int n = ringbuf_available(pcb->rcvbuf);
        return copyout(argp, &n, sizeof(n));
    }

    return inet_ifioctl(request, argp);
}

/** Queues our FIN, to go after what is in the send buffer. */
static void send_fin(struct tcp_pcb *pcb)
{
    if (pcb->fin_queued)
        return;

    pcb->fin_queued = 1;

    if (pcb->state == TCP_CLOSE_WAIT)
        pcb->state = TCP_LAST_ACK;
    else
        pcb->state = TCP_FIN_WAIT_1;

    tcp_output(pcb);
}

/** Shuts down reading (readers see the end), writing (the FIN is sent), or
 * both, for `shutdown()`. */
static int socket_tcp_shut(struct file *file, int how)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->state == TCP_CLOSED || pcb->state == TCP_LISTEN || pcb->state == TCP_SYN_SENT)
        return -ENOTCONN;

    if (how & SHUT_RD) {
        pcb->shut_rd = 1;
        tcp_wakeup(pcb);
    }

    if ((how & SHUT_WR) && (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT ||
                            pcb->state == TCP_SYN_RECEIVED))
        send_fin(pcb);

    return 0;
}

/** Detaches the connection when the socket's last descriptor is closed:
 * the connection is closed, and freed once it is done. A listener resets
 * the connections not accepted yet. */
static int socket_tcp_shutdown(struct file *file, int how)
{
    struct tcp_pcb *pcb = file->socket->p;

    pcb->detached = 1;

    switch (pcb->state) {
        case TCP_LISTEN:
            /* reset the connections not accepted yet, and drop the ones
             * still being made */
            while (pcb->accept_head) {
                struct tcp_pcb *child = pcb->accept_head;
                tcp_send_rst(child);
                tcp_drop(child, 0);
            }

            for (struct tcp_pcb *p = tcp_pcbs, *next; p; p = next) {
                next = p->next;
                if (p->parent == pcb)
                    tcp_drop(p, 0);
            }

            tcp_pcb_free(pcb);
            break;

        case TCP_CLOSED:
        case TCP_SYN_SENT:
            tcp_pcb_free(pcb);
            break;

        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
            /* there is data no one will read: reset (RFC 2525) */
            if (ringbuf_available(pcb->rcvbuf) || pcb->ooo) {
                tcp_send_rst(pcb);
                tcp_drop(pcb, 0);
                break;
            }

            send_fin(pcb);
            break;

        case TCP_FIN_WAIT_2:
            tcp_fin_wait_2(pcb);
            break;
    }

    kfree(file->socket);
    file->socket = NULL;
    return 0;
}

static struct sock_ops socket_tcp_ops = {
    .accept     = socket_tcp_accept,
    .bind       = socket_tcp_bind,
    .connect    = socket_tcp_connect,
    .listen     = socket_tcp_listen,
    .recv       = socket_tcp_recv,
    .send       = socket_tcp_send,
    .can_read   = socket_tcp_can_read,
    .can_write  = socket_tcp_can_write,
    .wait_queue = socket_tcp_wait_queue,
    .getname    = socket_tcp_getname,
    .getsockopt = socket_tcp_getsockopt,
    .setsockopt = socket_tcp_setsockopt,
    .ioctl      = socket_tcp_ioctl,
    .shut       = socket_tcp_shut,
    .shutdown   = socket_tcp_shutdown,
};
