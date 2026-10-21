/*
 * TCP's connections (struct tcp_pcb) and their sockets (SOCK_STREAM)
 *
 * Every connection is on a list (the ports in use, the listeners), and
 * once it has both ends' addresses and ports in a hashmap by them, which
 * segments are looked up in. A socket has its connection; the connection
 * stays when the socket is closed (detached) until it is done: its data
 * and FIN sent, TIME_WAIT over. One a listener made has no socket until
 * accept() takes it.
 *
 * Received segments and the timers run in the network's thread; sending
 * in the callers' system calls. Data from and to user space goes through
 * a buffer on the stack: copying it may fault and sleep, while the
 * network's thread changes the rings.
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

#define PORT_FIRST      49152   /* the ephemeral ports */
#define PORT_LAST       65535

#define MSL             30000000000ULL      /* 30 s: TIME_WAIT is 2 MSL */
#define FIN_WAIT_2_TIME 60000000000ULL      /* a closed socket's, waiting for the FIN */
#define RTO_INITIAL     1000000000ULL

#define KEEP_IDLE       7200000000000ULL    /* 2 h, then */
#define KEEP_INTERVAL   75000000000ULL      /* a probe every 75 s, */
#define KEEP_PROBES     9                   /* 9 unanswered: ETIMEDOUT */

#define COPY_CHUNK      512

struct tcp_pcb *tcp_pcbs;
static struct hashmap *conns;
static uint8_t iss_secret[16];

static struct sock_ops socket_tcp_ops;

/*
 * The connections
 */

struct tcp_key {
    uint32_t laddr, raddr;
    uint16_t lport, rport;
};

static hash_t key_hash(const struct tcp_key *key)
{
    return hashmap_digest(key, sizeof(*key));
}

static int key_eq(void *entry, void *k)
{
    struct tcp_pcb *pcb = entry;
    struct tcp_key *key = k;

    return pcb->laddr == key->laddr && pcb->raddr == key->raddr &&
           pcb->lport == key->lport && pcb->rport == key->rport;
}

struct tcp_pcb *tcp_lookup(uint32_t laddr, uint16_t lport, uint32_t raddr, uint16_t rport)
{
    struct tcp_key key = {laddr, raddr, lport, rport};
    struct hashmap_node *node = conns ? hashmap_lookup(conns, key_hash(&key), &key) : NULL;

    return node ? node->entry : NULL;
}

/* by its addresses and ports, which it has now */
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

/* RFC 6528: a clock (4 us), and a hash of the connection with a secret */
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

void tcp_wakeup(struct tcp_pcb *pcb)
{
    thread_queue_wakeup(&pcb->wait);
}

/* taken off its listener's queues (it is done, or accept() took it) */
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

/* CLOSED: freed if nothing has it anymore (its socket keeps it, out of
 * the lookups) */
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

/* the connection is lost: `err' for its socket (0: none) */
void tcp_drop(struct tcp_pcb *pcb, int err)
{
    pcb->error = err;
    tcp_closed(pcb);
}

void tcp_established(struct tcp_pcb *pcb)
{
    pcb->state = TCP_ESTABLISHED;
    pcb->connected = 1;

    /* RFC 5681's initial window */
    pcb->cwnd = MIN(4U * pcb->mss, MAX(2U * pcb->mss, 4380U));
    pcb->snd_nxt = pcb->snd_max = pcb->snd_una;

    tcp_keepalive_reset(pcb);

    /* a listener's: for accept() */
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

static void timer2_closed(void *arg)
{
    tcp_closed(arg);
}

void tcp_time_wait(struct tcp_pcb *pcb)
{
    pcb->state = TCP_TIME_WAIT;
    callout_stop(&pcb->rexmt);
    net_callout_set(&pcb->timer2, 2 * MSL, timer2_closed, pcb);
    tcp_wakeup(pcb);
}

/* our FIN acked: a closed socket's waits for the peer's a while */
void tcp_fin_wait_2(struct tcp_pcb *pcb)
{
    if (pcb->detached)
        net_callout_set(&pcb->timer2, FIN_WAIT_2_TIME, timer2_closed, pcb);
}

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

/* the peer was heard from */
void tcp_keepalive_reset(struct tcp_pcb *pcb)
{
    pcb->keep_probes = 0;

    if (pcb->keepalive && pcb->state == TCP_ESTABLISHED)
        net_callout_set(&pcb->timer2, KEEP_IDLE, keepalive_timeout, pcb);
}

/*
 * Ports
 */

/* (addr, port) is taken: by a listener, or by anyone unless reuseaddr */
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

static int bind_port(struct tcp_pcb *pcb, uint32_t addr, uint16_t port)
{
    if (!port) {
        static uint16_t next = PORT_FIRST;

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
    socket->ref      = 1;

    file->flags |= FILE_SOCKET;
    file->socket = socket;
    return 0;
}

static int nonblock(struct file *file, int flags)
{
    return (file->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
}

static uint64_t deadline(uint64_t timeo)
{
    return timeo ? arch_rtime_ns() + timeo : 0;
}

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

    /* the ports below 1024 are root's */
    if (ntohs(port) && ntohs(port) < 1024 && curproc->uid)
        return -EACCES;

    return bind_port(pcb, ip, port);
}

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

static int socket_tcp_accept(struct file *file, struct file *conn, const struct sockaddr *addr, socklen_t *len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->state != TCP_LISTEN)
        return -EINVAL;

    if (addr && (uaccess_check(len, sizeof(*len)) || uaccess_check(addr, *len)))
        return -EFAULT;

    uint64_t until = deadline(pcb->rcvtimeo);

    while (!pcb->accept_head) {
        if (nonblock(file, 0))
            return -EAGAIN;

        if (sched_wait(&pcb->wait, until))
            return -EAGAIN;

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
    socket->ref = 1;

    conn->socket = socket;
    conn->flags  = FILE_SOCKET;
    conn->offset = 0;

    if (addr)
        inet_sockaddr_put((struct sockaddr *) addr, len, child->raddr, child->rport);

    return 0;
}

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
            return -EALREADY;
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

    while (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RECEIVED)
        sched_wait(&pcb->wait, 0);

    if (pcb->connected && pcb->state != TCP_CLOSED)
        return 0;

    err = pcb->error ? pcb->error : ECONNREFUSED;
    pcb->error = 0;
    return -err;
}

/* the states data goes out in */
static int can_send(struct tcp_pcb *pcb)
{
    return (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) && !pcb->fin_queued;
}

static ssize_t socket_tcp_send(struct file *file, void *buf, size_t len, int flags,
                               const struct sockaddr *to, socklen_t tolen)
{
    struct tcp_pcb *pcb = file->socket->p;
    uint64_t until = deadline(pcb->sndtimeo);
    size_t sent = 0;
    char chunk[COPY_CHUNK];

    if (flags & MSG_OOB)
        return -EOPNOTSUPP;

    while (sent < len) {
        if (pcb->error) {
            int err = pcb->error;
            pcb->error = 0;
            return sent ? (ssize_t) sent : -err;
        }

        /* connecting: once it is */
        if (pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RECEIVED) {
            if (nonblock(file, flags))
                return sent ? (ssize_t) sent : -EAGAIN;

            if (sched_wait(&pcb->wait, until))
                return sent ? (ssize_t) sent : -EAGAIN;

            continue;
        }

        if (!can_send(pcb))
            return sent ? (ssize_t) sent : pcb->connected ? -EPIPE : -ENOTCONN;

        size_t room = TCP_BUFSIZE - 1 - ringbuf_available(pcb->sndbuf);

        if (!room) {
            tcp_output(pcb);

            if (nonblock(file, flags))
                return sent ? (ssize_t) sent : -EAGAIN;

            if (sched_wait(&pcb->wait, until))
                return sent ? (ssize_t) sent : -EAGAIN;

            continue;
        }

        size_t n = MIN(MIN(room, len - sent), sizeof(chunk));

        memcpy(chunk, (char *) buf + sent, n);
        ringbuf_write(pcb->sndbuf, n, chunk);
        sent += n;
    }

    tcp_output(pcb);
    return sent;
}

/* the window opened by what was read: the peer is told, if it moved
 * enough (tcp_rcv_window) */
static void window_update(struct tcp_pcb *pcb)
{
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_FIN_WAIT_1 &&
        pcb->state != TCP_FIN_WAIT_2)
        return;

    uint32_t cur = SEQ_GT(pcb->rcv_adv, pcb->rcv_nxt) ? pcb->rcv_adv - pcb->rcv_nxt : 0;

    if (tcp_rcv_window(pcb) > cur)
        tcp_send_ack(pcb);
}

static ssize_t socket_tcp_recv(struct file *file, void *buf, size_t len, int flags,
                               struct sockaddr *from, socklen_t *fromlen)
{
    struct tcp_pcb *pcb = file->socket->p;
    uint64_t until = deadline(pcb->rcvtimeo);
    size_t got = 0;
    char chunk[COPY_CHUNK];

    if (flags & MSG_OOB)
        return -EOPNOTSUPP;

    if (pcb->state == TCP_LISTEN)
        return -ENOTCONN;

    while (got < len) {
        size_t avail = ringbuf_available(pcb->rcvbuf);

        if ((flags & MSG_PEEK) && avail > got) {
            size_t n = ringbuf_read_noconsume(pcb->rcvbuf, got, MIN(len - got, sizeof(chunk)), chunk);
            memcpy((char *) buf + got, chunk, n);
            got += n;
            continue;
        }

        if (avail && !(flags & MSG_PEEK)) {
            size_t n = ringbuf_read(pcb->rcvbuf, MIN(len - got, sizeof(chunk)), chunk);
            memcpy((char *) buf + got, chunk, n);
            got += n;
            window_update(pcb);
            continue;
        }

        /* some, and not waiting for all of it */
        if (got && (!(flags & MSG_WAITALL) || (flags & MSG_PEEK)))
            break;

        if (pcb->error) {
            int err = pcb->error;
            pcb->error = 0;
            return got ? (ssize_t) got : -err;
        }

        /* the end */
        if (pcb->fin_received || pcb->shut_rd || (pcb->state == TCP_CLOSED && pcb->connected))
            break;

        if (pcb->state == TCP_CLOSED)
            return -ENOTCONN;

        if (nonblock(file, flags))
            return got ? (ssize_t) got : -EAGAIN;

        if (sched_wait(&pcb->wait, until))
            return got ? (ssize_t) got : -EAGAIN;
    }

    if (from && fromlen)
        inet_sockaddr_put(from, fromlen, pcb->raddr, pcb->rport);

    return got;
}

static int socket_tcp_can_read(struct file *file, size_t len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->state == TCP_LISTEN)
        return pcb->accept_head != NULL;

    /* data, the end, an error */
    return ringbuf_available(pcb->rcvbuf) || pcb->fin_received || pcb->error || pcb->shut_rd ||
           (pcb->state == TCP_CLOSED && pcb->connected);
}

static int socket_tcp_can_write(struct file *file, size_t len)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (pcb->error || (pcb->connected && !can_send(pcb)))
        return 1;

    return can_send(pcb) && ringbuf_available(pcb->sndbuf) < TCP_BUFSIZE - 1;
}

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

static int int_get(void *val, socklen_t *len, int v)
{
    if (*len < sizeof(int))
        return -EINVAL;

    *(int *) val = v;
    *len = sizeof(int);
    return 0;
}

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
                /* accepted, the peer's and the interface's decide */
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

    /* SO_SNDBUF, SO_RCVBUF, SO_LINGER: the socket layer accepts them */
    return -ENOPROTOOPT;
}

static int socket_tcp_ioctl(struct file *file, int request, void *argp)
{
    struct tcp_pcb *pcb = file->socket->p;

    if (request == FIONREAD) {
        if (uaccess_check(argp, sizeof(int)))
            return -EFAULT;

        *(int *) argp = ringbuf_available(pcb->rcvbuf);
        return 0;
    }

    return inet_ifioctl(request, argp);
}

/* our FIN, after what is in sndbuf */
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

/* the last descriptor is closed: the connection ends, and goes when done */
static int socket_tcp_shutdown(struct file *file, int how)
{
    struct tcp_pcb *pcb = file->socket->p;

    pcb->detached = 1;

    switch (pcb->state) {
        case TCP_LISTEN:
            /* the connections not accepted yet are reset */
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
            /* data no one will read: reset (RFC 2525) */
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
    .getname    = socket_tcp_getname,
    .getsockopt = socket_tcp_getsockopt,
    .setsockopt = socket_tcp_setsockopt,
    .ioctl      = socket_tcp_ioctl,
    .shut       = socket_tcp_shut,
    .shutdown   = socket_tcp_shutdown,
};
