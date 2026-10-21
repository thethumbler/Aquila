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
 *
 * The frames are taken up the stack by the network's thread, [net], a
 * thread of process 0 (seen in /proc/0/status).
 */

#include "../lib/aqtest.h"

#include <aquila/if.h>
#include <aquila/in.h>
#include <aquila/socket.h>

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
    check("SIOCGIFCONF: eth0", ifc.ifc_len == sizeof(struct ifreq) &&
          eq(list[0].ifr_name, "eth0", 5) && ifaddr(&list[0].ifr_addr) == ip4(10, 0, 2, 15), 1);

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
    check("SIOCGIFINDEX: it", ifr.ifr_ifindex, 1);
    check("SIOCGIFHWADDR", ifreq(fd, SIOCGIFHWADDR, &ifr), 0);
    check("SIOCGIFHWADDR: QEMU's", ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER &&
          eq(ifr.ifr_hwaddr.sa_data, "\x52\x54\x00\x12\x34\x56", 6), 1);
    check("SIOCGIFADDR: no such interface", ({
        for (int i = 0; i < IFNAMSIZ; ++i) ifr.ifr_name[i] = "nope"[i < 4 ? i : 4];
        SC(SYS_ioctl, fd, SIOCGIFADDR, &ifr); }), -ENODEV);

    SC(SYS_close, fd, 0, 0);
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

    SC(SYS_close, fd, 0, 0);
    SC(SYS_close, go, 0, 0);
}

void aq_main(void)
{
    aq_begin("net");

    kernel_thread();
    loopback();
    interfaces();
    peer();

    aq_end();
}
