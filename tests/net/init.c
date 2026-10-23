/*
 * The network seen from a program: UDP and ping sockets, their options,
 * and the interfaces' requests
 *
 * First on its own, over loopback; then with tests/net/peer.py, which
 * plays 10.0.2.2 (the gateway) and 10.0.2.3 on the e1000's link. The
 * peer sends "go" to port 9000 once it's done with its own checks; then
 * each step here is answered by it:
 *
 * - a datagram to 10.0.2.3:5001, which the kernel has to ARP for first,
 *   comes back
 * - a ping to 10.0.2.2 is answered
 * - a datagram to 10.0.2.2:5002 is answered by "port unreachable"
 * - a datagram to 192.0.2.1:5003 goes through the gateway, and comes back
 * - a broadcast to 10.0.2.255:5004 goes (the peer checks it)
 * - a TCP connection to 10.0.2.2:6000, which the peer plays segment by
 *   segment: 200 bytes from it (out of order), 6500 to it (losses, a
 *   zero window), then a reset
 * - another, to 10.0.2.2:6002: the end of 200 bytes out of order, then
 *   all of them again with the FIN; the answer, and closed with a FIN
 *   (what waited came again before the FIN: it isn't data unread, which
 *   would make the close a reset)
 *
 * TCP over loopback first, both ends here (and a child process).
 *
 * The frames are taken up the stack by the network's thread, [net], a
 * thread of process 0 (seen in /proc/0/status).
 */

#include "../lib/aqtest.h"

#include <aquila/if.h>
#include <aquila/in.h>
#include <aquila/poll.h>
#include <aquila/socket.h>
#include <aquila/tcp.h>

/* the kernel's struct timeval */
struct tv {
    uint64_t sec;
    long usec;
};

struct icmp {
    uint8_t  type, code;
    uint16_t checksum, id, seq;
    char data[32];
};

static uint16_t htons(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }

static uint32_t ip4(int a, int b, int c, int d)
{
    return (uint32_t) a | (uint32_t) b << 8 | (uint32_t) c << 16 | (uint32_t) d << 24;
}

static struct sockaddr_in sin(uint32_t addr, int port)
{
    struct sockaddr_in s = {0};
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    s.sin_addr.s_addr = addr;
    return s;
}

static int eq(const void *a, const void *b, int n)
{
    for (int i = 0; i < n; ++i)
        if (((const char *) a)[i] != ((const char *) b)[i])
            return 0;
    return 1;
}

static int udp(void)
{
    return SC(SYS_socket, AF_INET, SOCK_DGRAM, 0);
}

/* /proc/meminfo's MemFree, in KiB (procfs is on /mnt: kernel_thread()) */
static int memfree(void)
{
    static char text[512];
    int fd = SC(SYS_open, "/mnt/meminfo", O_RDONLY, 0);
    int n = fd < 0 ? 0 : SC(SYS_read, fd, text, sizeof(text) - 1);
    SC(SYS_close, fd, 0, 0);

    for (int i = 0; i + 8 < n; ++i) {
        if (text[i] == 'M' && text[i + 3] == 'F' && text[i + 7] == ':') {
            int kb = 0;
            for (int j = i + 8; j < n && (text[j] == ' ' || (text[j] >= '0' && text[j] <= '9')); ++j)
                if (text[j] != ' ')
                    kb = kb * 10 + text[j] - '0';
            return kb;
        }
    }

    return -1;
}

static int bind_to(int fd, uint32_t addr, int port)
{
    struct sockaddr_in s = sin(addr, port);
    return SC(SYS_bind, fd, &s, sizeof(s));
}

static int sendto(int fd, const void *buf, int len, uint32_t addr, int port)
{
    struct sockaddr_in s = sin(addr, port);
    struct aquila_socket_io_args a = {fd, (void *) buf, len, 0, &s, 0};
    unsigned int slen = sizeof(s);
    a.addrlen = &slen;
    return SC(SYS_send, &a, 0, 0);
}

static int recvfrom(int fd, void *buf, int len, int flags, struct sockaddr_in *from)
{
    unsigned int flen = sizeof(*from);
    struct aquila_socket_io_args a = {fd, buf, len, flags, from, &flen};
    return SC(SYS_recv, &a, 0, 0);
}

static int getopt(int fd, int name, void *val, unsigned int len)
{
    return SC5(SYS_getsockopt, fd, SOL_SOCKET, name, val, &len);
}

static int setopt(int fd, int name, const void *val, unsigned int len)
{
    return SC5(SYS_setsockopt, fd, SOL_SOCKET, name, val, len);
}

static int intopt(int fd, int name)
{
    int v = -1;
    int r = getopt(fd, name, &v, sizeof(v));
    return r ? r : v;
}

static void timeout(int fd, int ms)
{
    struct tv tv = {ms / 1000, ms % 1000 * 1000};
    setopt(fd, SO_RCVTIMEO, &tv, sizeof(tv));
}

static int port_of(int fd)
{
    struct sockaddr_in s;
    unsigned int len = sizeof(s);

    if (SC(SYS_getsockname, fd, &s, &len))
        return -1;

    return htons(s.sin_port);
}

static int ifreq(int fd, int req, struct ifreq *ifr)
{
    for (int i = 0; i < IFNAMSIZ; ++i)
        ifr->ifr_name[i] = "eth0"[i < 4 ? i : 4];

    return SC(SYS_ioctl, fd, req, ifr);
}

static uint32_t ifaddr(const struct sockaddr *a)
{
    return ((const struct sockaddr_in *) a)->sin_addr.s_addr;
}

/* an echo request through a ping socket to `addr', its reply checked */
static void ping(const char *name, uint32_t addr, int seq)
{
    int fd = SC(SYS_socket, AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    struct icmp req = {8, 0, 0, 0, htons(seq), "aquila ping data, 32 bytes long"}, rep;
    struct sockaddr_in from;

    timeout(fd, 5000);

    check(name, sendto(fd, &req, sizeof(req), addr, 0), sizeof(req));
    int n = recvfrom(fd, &rep, sizeof(rep), 0, &from);
    check(name, n, sizeof(rep));
    check(name, rep.type == 0 && rep.seq == htons(seq) && eq(rep.data, req.data, 32) &&
          from.sin_addr.s_addr == addr, 1);

    SC(SYS_close, fd, 0, 0);
}

static void loopback(void)
{
    uint32_t lo = ip4(127, 0, 0, 1);
    char buf[64];
    struct sockaddr_in from;

    int s = udp(), c = udp(), o = udp();
    check("socket", s >= 0 && c >= 0 && o >= 0, 1);
    check("SO_TYPE", intopt(s, SO_TYPE), SOCK_DGRAM);

    check("bind", bind_to(s, lo, 7000), 0);
    check("bind: in use", bind_to(o, 0, 7000), -EADDRINUSE);
    check("bind: below 1024, root", bind_to(o, 0, 1000), 0);

    struct sockaddr_in name;
    unsigned int len = sizeof(name);
    check("getsockname", SC(SYS_getsockname, s, &name, &len), 0);
    check("getsockname: it", name.sin_family == AF_INET && name.sin_port == htons(7000) &&
          name.sin_addr.s_addr == lo && len == sizeof(name), 1);
    check("getpeername: not connected", SC(SYS_getpeername, s, &name, &len), -ENOTCONN);

    check("sendto", sendto(c, "hello", 5, lo, 7000), 5);
    check("sendto: bound to an ephemeral port", port_of(c) >= 49152, 1);

    int avail = -1;
    check("FIONREAD", SC(SYS_ioctl, s, FIONREAD, &avail), 0);
    check("FIONREAD: the datagram", avail, 5);

    check("recvfrom", recvfrom(s, buf, sizeof(buf), 0, &from), 5);
    check("recvfrom: the data", eq(buf, "hello", 5), 1);
    check("recvfrom: from", from.sin_addr.s_addr == lo && htons(from.sin_port) == port_of(c), 1);

    check("recvfrom: nothing", recvfrom(s, buf, sizeof(buf), MSG_DONTWAIT, &from), -EAGAIN);

    /* a burst nobody reads: the queue keeps what its buffers allow (128
     * datagrams), not as many as its bytes would (empty ones have none);
     * read, their pages go back but for a few (64 netbufs, 128 KiB) */
    int before = memfree();
    for (int i = 0; i < 1000; ++i)
        sendto(c, "", 0, lo, 7000);
    int queued = 0;
    while (recvfrom(s, buf, sizeof(buf), MSG_DONTWAIT, &from) >= 0)
        ++queued;
    check("a burst of empty datagrams: the queue holds 128", queued, 128);

    /* four queues full (1 MiB of netbufs, more than any were free), all
     * read: what's kept is the 64 free netbufs, 128 KiB */
    int q[4];
    for (int k = 0; k < 4; ++k) {
        q[k] = udp();
        bind_to(q[k], lo, 7100 + k);
        for (int i = 0; i < 128; ++i)
            sendto(c, "", 0, lo, 7100 + k);
    }
    for (int k = 0; k < 4; ++k) {
        while (recvfrom(q[k], buf, sizeof(buf), MSG_DONTWAIT, &from) >= 0)
            ;
        SC(SYS_close, q[k], 0, 0);
    }
    check("their pages back", before > 0 && memfree() >= before - 136, 1);

    timeout(s, 100);
    check("SO_RCVTIMEO", recvfrom(s, buf, sizeof(buf), 0, &from), -EAGAIN);

    /* nobody there: "port unreachable" comes back */
    check("sendto: nobody", sendto(c, "x", 1, lo, 7001), 1);
    check("port unreachable: SO_ERROR", intopt(c, SO_ERROR), ECONNREFUSED);
    check("SO_ERROR: reported once", intopt(c, SO_ERROR), 0);
    sendto(c, "x", 1, lo, 7001);
    check("port unreachable: recvfrom", recvfrom(c, buf, sizeof(buf), MSG_DONTWAIT, &from), -ECONNREFUSED);

    check("broadcast: not without SO_BROADCAST", sendto(c, "x", 1, 0xffffffff, 7000), -EACCES);
    int one = 1;
    check("SO_BROADCAST", setopt(c, SO_BROADCAST, &one, sizeof(one)), 0);
    check("SO_BROADCAST: set", intopt(c, SO_BROADCAST), 1);

    ping("ping 127.0.0.1", lo, 1);
    ping("ping 10.0.2.15", ip4(10, 0, 2, 15), 2);

    check("close", SC(SYS_close, s, 0, 0), 0);
    s = udp();
    check("bind: free again after close", bind_to(s, lo, 7000), 0);

    SC(SYS_close, s, 0, 0);
    SC(SYS_close, c, 0, 0);
    SC(SYS_close, o, 0, 0);
}

static void interfaces(void)
{
    int fd = udp();
    struct ifreq ifr, list[4];
    struct ifconf ifc = {sizeof(list), {(char *) list}};

    check("SIOCGIFCONF", SC(SYS_ioctl, fd, SIOCGIFCONF, &ifc), 0);
    check("SIOCGIFCONF: lo, then eth0", ifc.ifc_len == 2 * sizeof(struct ifreq) &&
          eq(list[0].ifr_name, "lo", 3) && ifaddr(&list[0].ifr_addr) == ip4(127, 0, 0, 1) &&
          eq(list[1].ifr_name, "eth0", 5) && ifaddr(&list[1].ifr_addr) == ip4(10, 0, 2, 15), 1);

    struct ifreq lo = {"lo"};
    check("lo: SIOCGIFFLAGS", SC(SYS_ioctl, fd, SIOCGIFFLAGS, &lo), 0);
    check("lo: up, loopback", lo.ifr_flags & (IFF_UP | IFF_LOOPBACK | IFF_BROADCAST), IFF_UP | IFF_LOOPBACK);
    SC(SYS_ioctl, fd, SIOCGIFNETMASK, &lo);
    check("lo: 127/8", ifaddr(&lo.ifr_netmask), ip4(255, 0, 0, 0));
    SC(SYS_ioctl, fd, SIOCGIFINDEX, &lo);
    check("lo: index 1", lo.ifr_ifindex, 1);

    check("SIOCGIFADDR", ifreq(fd, SIOCGIFADDR, &ifr), 0);
    check("SIOCGIFADDR: it", ifaddr(&ifr.ifr_addr) == ip4(10, 0, 2, 15), 1);
    check("SIOCGIFNETMASK", ifreq(fd, SIOCGIFNETMASK, &ifr), 0);
    check("SIOCGIFNETMASK: it", ifaddr(&ifr.ifr_netmask) == ip4(255, 255, 255, 0), 1);
    check("SIOCGIFBRDADDR", ifreq(fd, SIOCGIFBRDADDR, &ifr), 0);
    check("SIOCGIFBRDADDR: it", ifaddr(&ifr.ifr_broadaddr) == ip4(10, 0, 2, 255), 1);
    check("SIOCGIFFLAGS", ifreq(fd, SIOCGIFFLAGS, &ifr), 0);
    check("SIOCGIFFLAGS: up", ifr.ifr_flags & IFF_UP, IFF_UP);
    check("SIOCGIFMTU", ifreq(fd, SIOCGIFMTU, &ifr), 0);
    check("SIOCGIFMTU: it", ifr.ifr_mtu, 1500);
    check("SIOCGIFINDEX", ifreq(fd, SIOCGIFINDEX, &ifr), 0);
    check("SIOCGIFINDEX: it", ifr.ifr_ifindex, 2);
    check("SIOCGIFHWADDR", ifreq(fd, SIOCGIFHWADDR, &ifr), 0);
    check("SIOCGIFHWADDR: QEMU's", ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER &&
          eq(ifr.ifr_hwaddr.sa_data, "\x52\x54\x00\x12\x34\x56", 6), 1);
    check("SIOCGIFADDR: no such interface", ({
        for (int i = 0; i < IFNAMSIZ; ++i) ifr.ifr_name[i] = "nope"[i < 4 ? i : 4];
        SC(SYS_ioctl, fd, SIOCGIFADDR, &ifr); }), -ENODEV);

    /* what a DHCP client does: bound to the interface, and to the
     * broadcast address */
    int d = udp(), e = udp();
    char dev[IFNAMSIZ] = {0};
    unsigned int devlen = sizeof(dev);

    check("struct sockaddr holds a sockaddr_in", sizeof(struct sockaddr), sizeof(struct sockaddr_in));
    check("SO_BINDTODEVICE", setopt(d, SO_BINDTODEVICE, "eth0", 5), 0);
    check("SO_BINDTODEVICE: get", SC5(SYS_getsockopt, d, SOL_SOCKET, SO_BINDTODEVICE, dev, &devlen), 0);
    check("SO_BINDTODEVICE: eth0", eq(dev, "eth0", 5), 1);
    check("SO_BINDTODEVICE: no such interface", setopt(e, SO_BINDTODEVICE, "nope", 5), -ENODEV);
    check("bind: the broadcast address", bind_to(d, 0xffffffff, 68), 0);
    check("bind: someone else's address", bind_to(e, ip4(10, 0, 2, 99), 68), -EADDRNOTAVAIL);
    SC(SYS_close, d, 0, 0);
    SC(SYS_close, e, 0, 0);

    SC(SYS_close, fd, 0, 0);
}

/*
 * TCP over loopback
 */

static int tcp(void)
{
    return SC(SYS_socket, AF_INET, SOCK_STREAM, 0);
}

static int tcp_listen(uint32_t addr, int port)
{
    int fd = tcp();

    if (bind_to(fd, addr, port) || SC(SYS_listen, fd, 8, 0))
        return -1;

    return fd;
}

static int tcp_connect(int fd, uint32_t addr, int port)
{
    struct sockaddr_in s = sin(addr, port);
    return SC(SYS_connect, fd, &s, sizeof(s));
}

static int tcp_accept(int fd)
{
    return SC(SYS_accept, fd, 0, 0);
}

static int send_all(int fd, const void *buf, int len)
{
    struct aquila_socket_io_args a = {fd, (void *) buf, len, 0, 0, 0};
    return SC(SYS_send, &a, 0, 0);
}

static int recv_some(int fd, void *buf, int len, int flags)
{
    struct aquila_socket_io_args a = {fd, buf, len, flags, 0, 0};
    return SC(SYS_recv, &a, 0, 0);
}

/* len bytes, or what there is until the end */
static int recv_all(int fd, void *buf, int len)
{
    int got = 0, n;

    while (got < len && (n = recv_some(fd, (char *) buf + got, len - got, 0)) > 0)
        got += n;

    return got ? got : n;
}

static int tcpopt(int fd, int name)
{
    int v = -1;
    unsigned int len = sizeof(v);
    int r = SC5(SYS_getsockopt, fd, IPPROTO_TCP, name, &v, &len);
    return r ? r : v;
}

static int peer_port(int fd)
{
    struct sockaddr_in s;
    unsigned int len = sizeof(s);

    if (SC(SYS_getpeername, fd, &s, &len))
        return -1;

    return htons(s.sin_port);
}

#define BULK    (1024 * 1024)
static unsigned char bulk[BULK];

static unsigned char pattern(int i)
{
    return (unsigned char) (i * 7 + (i >> 9));
}

/* the child: connects, sends a megabyte, reads it back checked from the
 * server (exit 0 if it was right) */
static uint32_t bulk_addr;

static void bulk_client(void)
{
    int c = tcp();

    if (tcp_connect(c, bulk_addr, 8001))
        SC(SYS_exit, 1, 0, 0);

    for (int i = 0; i < BULK; ++i)
        bulk[i] = pattern(i);

    if (send_all(c, bulk, BULK) != BULK)
        SC(SYS_exit, 2, 0, 0);

    SC(SYS_shutdown, c, SHUT_WR, 0);

    for (int i = 0; i < BULK; ++i)
        bulk[i] = 0;

    if (recv_all(c, bulk, BULK) != BULK)
        SC(SYS_exit, 3, 0, 0);

    for (int i = 0; i < BULK; ++i)
        if (bulk[i] != pattern(i))
            SC(SYS_exit, 4, 0, 0);

    /* the server closed: the end */
    char x;
    SC(SYS_exit, recv_some(c, &x, 1, 0) == 0 ? 0 : 5, 0, 0);
}

/* a child that writes to a connection it shut down: SIGPIPE kills it */
static int sigpipe_fd;

static void sigpipe_child(void)
{
    struct aquila_sigaction dfl = {0 /* SIG_DFL */, 0, 0};
    SC(SYS_sigaction, SIGPIPE, &dfl, 0);
    send_all(sigpipe_fd, "x", 1);
}

static void tcp_loopback(void)
{
    uint32_t lo = ip4(127, 0, 0, 1);
    char buf[64];

    /* EPIPE, not SIGPIPE (checked on its own, below) */
    struct aquila_sigaction ign = {1 /* SIG_IGN */, 0, 0};
    SC(SYS_sigaction, SIGPIPE, &ign, 0);

    int l = tcp_listen(lo, 8000);
    check("tcp: listen", l >= 0, 1);
    check("tcp: SO_ACCEPTCONN", intopt(l, SO_ACCEPTCONN), 1);
    check("tcp: SO_TYPE", intopt(l, SO_TYPE), SOCK_STREAM);

    int c = tcp();
    check("tcp: connect", tcp_connect(c, lo, 8000), 0);
    check("tcp: connect again", tcp_connect(c, lo, 8000), -EISCONN);

    int s = tcp_accept(l);
    check("tcp: accept", s >= 0, 1);
    check("tcp: the ends' ports", peer_port(s) == port_of(c) && peer_port(c) == 8000 &&
          port_of(s) == 8000, 1);

    check("tcp: send", send_all(c, "hello, tcp", 10), 10);
    check("tcp: MSG_PEEK", recv_some(s, buf, 5, MSG_PEEK), 5);

    int avail = -1;
    SC(SYS_ioctl, s, FIONREAD, &avail);
    check("tcp: FIONREAD", avail, 10);

    check("tcp: recv", recv_all(s, buf, 10), 10);
    check("tcp: recv: the data", eq(buf, "hello, tcp", 10), 1);
    check("tcp: recv: nothing", recv_some(s, buf, 1, MSG_DONTWAIT), -EAGAIN);

    check("tcp: the other way", send_all(s, "back", 4), 4);
    check("tcp: the other way: recv", recv_all(c, buf, 4) == 4 && eq(buf, "back", 4), 1);

    int one = 1;
    check("tcp: TCP_NODELAY", SC5(SYS_setsockopt, c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)), 0);
    check("tcp: TCP_NODELAY: set", tcpopt(c, TCP_NODELAY), 1);
    check("tcp: TCP_MAXSEG", tcpopt(c, TCP_MAXSEG) > 0, 1);

    /* half-close: EOF one way, data still the other */
    check("tcp: shutdown", SC(SYS_shutdown, c, SHUT_WR, 0), 0);
    check("tcp: shutdown: EOF", recv_some(s, buf, 1, 0), 0);
    check("tcp: shutdown: send after", send_all(c, "x", 1), -EPIPE);
    sigpipe_fd = c;
    check("tcp: shutdown: send after: SIGPIPE", aq_child_status(sigpipe_child) & 0x7f, SIGPIPE);
    check("tcp: shutdown: the other way still", send_all(s, "late", 4), 4);
    check("tcp: shutdown: the other way: recv", recv_all(c, buf, 4) == 4 && eq(buf, "late", 4), 1);
    SC(SYS_close, s, 0, 0);
    check("tcp: closed: EOF", recv_some(c, buf, 1, 0), 0);
    SC(SYS_close, c, 0, 0);

    /* ports */
    int b = tcp();
    check("tcp: bind: a listener's port", bind_to(b, 0, 8000), -EADDRINUSE);
    SC(SYS_close, b, 0, 0);

    /* nobody there */
    c = tcp();
    check("tcp: connect: refused", tcp_connect(c, lo, 8999), -ECONNREFUSED);
    SC(SYS_close, c, 0, 0);

    /* closed with data unread: reset */
    c = tcp();
    tcp_connect(c, lo, 8000);
    s = tcp_accept(l);
    send_all(c, "unread", 6);
    recv_some(s, buf, 1, MSG_PEEK);
    SC(SYS_close, s, 0, 0);
    check("tcp: reset: recv", recv_some(c, buf, 1, 0), -ECONNRESET);

    check("tcp: reset: send", send_all(c, "x", 1), -EPIPE);
    SC(SYS_close, c, 0, 0);

    /* non-blocking: connect, then poll() until it's writable */
    c = tcp();
    SC(SYS_fcntl, c, F_SETFL, O_NONBLOCK);
    check("tcp: connect: non-blocking", tcp_connect(c, lo, 8000), -EINPROGRESS);

    struct pollfd pfd = {c, POLLOUT, 0};
    check("tcp: connect: poll", SC(SYS_poll, &pfd, 1, 2000), 1);
    check("tcp: connect: writable", pfd.revents & POLLOUT, POLLOUT);
    check("tcp: connect: SO_ERROR", intopt(c, SO_ERROR), 0);

    s = tcp_accept(l);
    pfd = (struct pollfd) {s, POLLIN, 0};
    check("tcp: poll: nothing to read", SC(SYS_poll, &pfd, 1, 0), 0);
    send_all(c, "p", 1);
    check("tcp: poll: readable", SC(SYS_poll, &pfd, 1, 2000) == 1 && (pfd.revents & POLLIN), 1);
    SC(SYS_close, s, 0, 0);
    SC(SYS_close, c, 0, 0);

    SC(SYS_fcntl, l, F_SETFL, O_NONBLOCK);
    check("tcp: accept: non-blocking", tcp_accept(l), -EAGAIN);
    SC(SYS_close, l, 0, 0);

    /* a megabyte each way, with a child */
    bulk_addr = lo;
    l = tcp_listen(lo, 8001);
    int pid = SC(SYS_fork, 0, 0, 0);

    if (!pid) {
        bulk_client();
        SC(SYS_exit, 9, 0, 0);
    }

    s = tcp_accept(l);
    int got = recv_all(s, bulk, BULK);
    int right = got == BULK;

    for (int i = 0; right && i < BULK; ++i)
        right = bulk[i] == pattern(i);

    check("tcp: a megabyte", got, BULK);
    check("tcp: a megabyte: the data", right, 1);
    check("tcp: a megabyte: then EOF", recv_some(s, buf, 1, 0), 0);
    check("tcp: a megabyte: back", send_all(s, bulk, BULK), BULK);
    SC(SYS_close, s, 0, 0);
    SC(SYS_close, l, 0, 0);

    int st = -1;
    SC(SYS_waitpid, pid, &st, 0);
    check("tcp: a megabyte: the child got it back", st, 0);
}

/* `needle' in the first `n' bytes of `s' */
static int has(const char *s, int n, const char *needle)
{
    int len = aq_strlen(needle);

    for (int i = 0; i + len <= n; ++i)
        if (eq(s + i, needle, len))
            return 1;

    return 0;
}

static void kernel_thread(void)
{
    static struct { char *dev, *opt; } no_args = {0, 0};
    static char st[1024];

    check("procfs", aq_mount("procfs", "/mnt", &no_args), 0);

    int fd = SC(SYS_open, "/mnt/0/status", O_RDONLY, 0);
    check("/proc/0/status", fd >= 0, 1);

    int n = SC(SYS_read, fd, st, sizeof(st));
    check("process 0: the kernel's", has(st, n, "name: [kernel]\npid: 0\n"), 1);
    check("process 0: the network's thread", has(st, n, "thread 1 net: "), 1);
    SC(SYS_close, fd, 0, 0);

    check("kill -1: not process 0", SC(SYS_kill, -1, 0, 0), -ESRCH);
}

static void peer(void)
{
    char buf[64];
    struct sockaddr_in from;
    uint32_t gw = ip4(10, 0, 2, 2), other = ip4(10, 0, 2, 3), far = ip4(192, 0, 2, 1);

    /* the peer is done with its checks */
    int go = udp();
    bind_to(go, 0, 9000);
    timeout(go, 60000);
    check("peer: go", recvfrom(go, buf, sizeof(buf), 0, &from), 2);
    check("peer: go, from it", from.sin_addr.s_addr == gw && htons(from.sin_port) == 5000, 1);

    int fd = udp();
    timeout(fd, 5000);

    /* ARP first */
    check("udp to 10.0.2.3", sendto(fd, "aquila-udp", 10, other, 5001), 10);
    check("udp from 10.0.2.3", recvfrom(fd, buf, sizeof(buf), 0, &from), 10);
    check("udp from 10.0.2.3: it", eq(buf, "aquila-udp", 10) && from.sin_addr.s_addr == other &&
          htons(from.sin_port) == 5001, 1);

    ping("ping 10.0.2.2", gw, 3);

    check("udp to a closed port", sendto(fd, "closed", 6, gw, 5002), 6);
    check("udp to a closed port: refused", recvfrom(fd, buf, sizeof(buf), 0, &from), -ECONNREFUSED);

    check("udp through the gateway", sendto(fd, "routed", 6, far, 5003), 6);
    check("udp through the gateway: back", recvfrom(fd, buf, sizeof(buf), 0, &from), 6);
    check("udp through the gateway: from", eq(buf, "routed", 6) && from.sin_addr.s_addr == far, 1);

    int one = 1;
    setopt(fd, SO_BROADCAST, &one, sizeof(one));
    check("udp broadcast", sendto(fd, "everyone", 8, ip4(10, 0, 2, 255), 5004), 8);

    /* TCP, the peer playing it by hand */
    static unsigned char in[200], out[6500];
    int t = tcp();

    check("tcp to the peer: connect", tcp_connect(t, gw, 6000), 0);
    check("tcp to the peer: 200 bytes", recv_all(t, in, 200), 200);

    int order = 1;
    for (int i = 0; i < 200; ++i)
        order &= in[i] == i;
    check("tcp to the peer: out of order, read in order", order, 1);

    for (int i = 0; i < 6500; ++i)
        out[i] = i * 7;
    check("tcp to the peer: 6500 bytes", send_all(t, out, 6500), 6500);
    check("tcp to the peer: reset", recv_some(t, in, 1, 0), -ECONNRESET);
    SC(SYS_close, t, 0, 0);

    /* the end out of order, then all of it again with the FIN */
    t = tcp();
    check("tcp to the peer, again: connect", tcp_connect(t, gw, 6002), 0);
    check("tcp to the peer, again: 200 bytes, then the end",
          recv_all(t, in, 200) == 200 && recv_some(t, in, 1, 0) == 0, 1);
    check("tcp to the peer, again: an answer", send_all(t, "ok", 2), 2);
    SC(SYS_close, t, 0, 0);

    SC(SYS_close, fd, 0, 0);
    SC(SYS_close, go, 0, 0);
}

void aq_main(void)
{
    aq_begin("net");

    kernel_thread();
    loopback();
    tcp_loopback();
    interfaces();
    peer();

    aq_end();
}
