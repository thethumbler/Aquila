/** \file
 * Network interfaces (see `net/netif.h`).
 *
 * A network driver registers its interface, hands it the frames it
 * receives (`netif_input()`), and sends the frames the interface gives it
 * (its `transmit` operation). lo, the loopback interface (127.0.0.1/8),
 * is always there and comes first; the others are eth0, eth1, ..., in the
 * order they are registered.
 *
 * Received frames wait for the network's thread (`[net]`, a kernel
 * thread), which takes them up the stack, `NET_BUDGET` at a time before
 * the other threads run again, and runs the network's callouts
 * (`net_callout_set()`). It is an urgent thread: when a driver's interrupt
 * wakes it, it runs as soon as the interrupt returns.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/kargs.h>
#include <core/module.h>
#include <core/panic.h>
#include <core/printk.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/netif.h>
#include <net/inet.h>
#include <sys/callout.h>
#include <sys/kthread.h>
#include <sys/sched.h>

#define NET_BUDGET      32      /* frames handled before other threads run */
#define NET_BACKLOG     256     /* the most frames that may wait */

struct netif *netifs;
static int ethers_nr;

static struct netif loopback;

static struct thread *net_thread;
static struct waitq net_wait;

static struct netbuf *rx_head, *rx_tail;
static size_t rx_count;

static struct callout_list net_callouts;

/** Parses an address "a.b.c.d" at `*s` into `*ip`, in network byte order,
 * and moves `*s` past it. Returns 0, or -1 if it isn't an address. */
static int parse_ipv4(const char **s, uint32_t *ip)
{
    uint32_t addr = 0;

    for (int i = 0; i < 4; ++i) {
        if (**s < '0' || **s > '9')
            return -1;

        uint32_t byte = 0;
        while (**s >= '0' && **s <= '9')
            byte = byte * 10 + (*(*s)++ - '0');

        if (byte > 255 || (i < 3 && *(*s)++ != '.'))
            return -1;

        addr = addr << 8 | byte;
    }

    *ip = htonl(addr);
    return 0;
}

/** Configures the first Ethernet interface from the kernel's command
 * line: `ip=address/prefix-length[:gateway]`. */
static void netif_config(struct netif *netif)
{
    const char *s = NULL;

    if (kargs_get("ip", &s) || !s)
        return;

    uint32_t ip, gateway = 0, prefix = 24;

    if (parse_ipv4(&s, &ip))
        goto bad;

    if (*s == '/') {
        ++s;
        prefix = 0;
        while (*s >= '0' && *s <= '9')
            prefix = prefix * 10 + (*s++ - '0');

        if (prefix > 32)
            goto bad;
    }

    if (*s == ':' && (++s, parse_ipv4(&s, &gateway)))
        goto bad;

    netif->ip      = ip;
    netif->netmask = htonl(prefix ? ~0U << (32 - prefix) : 0);
    netif->gateway = gateway;
    netif->flags  |= NETIF_UP;

    uint32_t a = ntohl(ip), g = ntohl(gateway);
    printk("net: %s %d.%d.%d.%d/%d, gateway %d.%d.%d.%d\n", netif->name,
           a >> 24, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff, prefix,
           g >> 24, (g >> 16) & 0xff, (g >> 8) & 0xff, g & 0xff);
    return;

bad:
    printk("net: ip=: not <address>/<prefix length>[:<gateway>]\n");
}

/** Writes a MAC address into `buf` as "xx:xx:xx:xx:xx:xx" (printk() has no
 * field widths) and returns `buf`. */
static char *mac_string(const uint8_t mac[6], char buf[18])
{
    static const char digits[] = "0123456789abcdef";

    for (int i = 0; i < 6; ++i) {
        buf[i * 3]     = digits[mac[i] >> 4];
        buf[i * 3 + 1] = digits[mac[i] & 0xf];
        buf[i * 3 + 2] = i < 5 ? ':' : '\0';
    }

    return buf;
}

/**
 * Sets a callout that the network's thread runs: `fn(arg)` is called
 * once `ns` ns have passed. TCP's timers use it.
 *
 * @param c    the callout
 * @param ns   the delay, in ns
 * @param fn   the function to call
 * @param arg  its argument
 */
void net_callout_set(struct callout *c, uint64_t ns, void (*fn)(void *), void *arg)
{
    callout_set_on(&net_callouts, c, ns, fn, arg);

    /* the thread may be sleeping until a later callout */
    if (net_thread)
        thread_wakeup(net_thread);
}

/** Runs the network's thread: it takes the received frames up the stack
 * and runs the network's callouts, then sleeps until the next frame or
 * callout. */
static void net_main(void *arg __unused)
{
    for (;;) {
        for (int n = 0; rx_head && n < NET_BUDGET; ++n) {
            struct netbuf *nb = rx_head;

            rx_head = nb->next;
            if (!rx_head)
                rx_tail = NULL;

            rx_count--;
            nb->next = NULL;

            if (nb->loopback)
                ipv4_input(nb->netif, nb, nb->netif->mac);
            else
                ether_input(nb->netif, nb);
        }

        callout_list_run(&net_callouts, arch_rtime_ns());

        /* if more frames wait, let the other threads run first */
        if (rx_head) {
            arch_yield();
            continue;
        }

        sched_wait(&net_wait, callout_list_next(&net_callouts));
    }
}

/**
 * Finds an interface by its name.
 *
 * @param name  the name (at most 16 characters, `IFNAMSIZ`)
 * @return the interface, or NULL
 */
struct netif *netif_find(const char *name)
{
    for (struct netif *netif = netifs; netif; netif = netif->next)
        if (!strncmp(netif->name, name, 16))
            return netif;

    return NULL;
}

static void loopback_init(void);

/**
 * Registers a network interface. An interface without a name gets the
 * next "ethN". The first Ethernet interface is configured from the
 * kernel's `ip=` argument. The network's thread is started with the first
 * interface.
 *
 * @param netif  the interface, with its MAC address and `transmit` set
 * @return 0
 */
int netif_register(struct netif *netif)
{
    /* lo comes first */
    if (netif != &loopback && !loopback.transmit)
        loopback_init();

    if (!net_thread) {
        if (kthread_create("net", net_main, NULL, &net_thread))
            panic("net: can't make the network's thread");

        net_thread->urgent = 1;
    }

    int n = -1;

    if (!netif->name[0]) {
        n = ethers_nr++;
        snprintf(netif->name, sizeof(netif->name), "eth%d", n);
    }

    if (!netif->mtu)
        netif->mtu = 1500;

    /* add it at the end, in the order they come */
    struct netif **p = &netifs;

    while (*p)
        p = &(*p)->next;

    netif->next = NULL;
    *p = netif;

    char mac[18];

    if (netif->flags & NETIF_LOOPBACK)
        printk("net: %s: loopback, 127.0.0.1/8\n", netif->name);
    else
        printk("net: %s: %s\n", netif->name, mac_string(netif->mac, mac));

    /* ip= configures the first Ethernet interface */
    if (n == 0)
        netif_config(netif);

    return 0;
}

/** Sends a frame on lo: a copy of it comes back in. Only frames sent
 * through lo explicitly get here (a broadcast, `SO_BINDTODEVICE`): packets
 * to 127.0.0.0/8 are turned around earlier, by ipv4_output(). */
static int loopback_transmit(struct netif *netif, struct netbuf *nb)
{
    struct netbuf *copy = netbuf_alloc();

    if (!copy)
        return -ENOBUFS;

    memcpy(netbuf_put(copy, nb->len), nb->data, nb->len);
    netif_input(netif, copy);
    return 0;
}

/** Sets up lo, the loopback interface, once. */
static void loopback_init(void)
{
    if (loopback.transmit)
        return;

    strcpy(loopback.name, "lo");
    loopback.ip       = htonl(0x7f000001);
    loopback.netmask  = htonl(0xff000000);
    loopback.flags    = NETIF_UP | NETIF_RUNNING | NETIF_LOOPBACK;
    loopback.mtu      = 1500;     /* about what a netbuf holds */
    loopback.transmit = loopback_transmit;

    netif_register(&loopback);
}

/** Sets up lo at boot, so that it is there even without a network card. */
static int loopback_module_init(void)
{
    loopback_init();
    return 0;
}

MODULE_INIT(loopback, loopback_module_init, NULL)

/** Queues a received frame for the network's thread, and wakes it up. The
 * frame is dropped if the interface is down or too many frames wait. */
static void rx_queue(struct netif *netif, struct netbuf *nb)
{
    if (!(netif->flags & NETIF_UP) || rx_count >= NET_BACKLOG) {
        netif->stats.rx_dropped++;
        netbuf_free(nb);
        return;
    }

    /* add it to the queue of the network's thread */
    nb->next = NULL;

    if (rx_tail)
        rx_tail->next = nb;
    else
        rx_head = nb;

    rx_tail = nb;
    rx_count++;

    thread_wakeup(net_thread);
}

/**
 * Hands the stack a frame that a driver received, possibly from an
 * interrupt. The netbuf is the stack's from now on.
 *
 * @param netif  the interface it came in on
 * @param nb     the frame
 */
void netif_input(struct netif *netif, struct netbuf *nb)
{
    netif->stats.rx_packets++;
    netif->stats.rx_bytes += nb->len;
    nb->netif = netif;
    nb->loopback = 0;

    rx_queue(netif, nb);
}

/**
 * Hands the stack an IPv4 packet sent to one of our own addresses, as if
 * it had come in through an interface.
 *
 * @param netif  the interface it is received on
 * @param nb     the packet, from its IPv4 header
 */
void netif_loopback(struct netif *netif, struct netbuf *nb)
{
    netif->stats.rx_packets++;
    netif->stats.rx_bytes += nb->len;
    nb->netif = netif;
    nb->loopback = 1;

    rx_queue(netif, nb);
}

/**
 * Sends a frame through an interface's driver. The netbuf stays the
 * caller's.
 *
 * @param netif  the interface
 * @param nb     the frame
 * @return 0, or the driver's negative errno
 */
int netif_output(struct netif *netif, struct netbuf *nb)
{
    int err = netif->transmit(netif, nb);

    if (err) {
        netif->stats.tx_errors++;
        return err;
    }

    netif->stats.tx_packets++;
    netif->stats.tx_bytes += nb->len;
    return 0;
}
