/*
 * Network interfaces (net/netif.h): drivers register theirs, give them
 * the frames they receive (netif_input) and send the frames given to
 * them (transmit)
 */

#include <core/system.h>
#include <core/kargs.h>
#include <core/printk.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/netif.h>
#include <net/inet.h>

struct netif *netifs;
static int netifs_nr;

/* "a.b.c.d" at *s, in the network's byte order; *s past it, -1 if it
 * isn't an address */
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

/* ip=<address>/<prefix length>[:<gateway>] for the first interface */
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

/* "xx:xx:xx:xx:xx:xx" (printk has no field widths) */
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

int netif_register(struct netif *netif)
{
    int n = netifs_nr++;

    snprintf(netif->name, sizeof(netif->name), "eth%d", n);

    if (!netif->mtu)
        netif->mtu = 1500;

    netif->next = netifs;
    netifs = netif;

    char mac[18];
    printk("net: %s: %s\n", netif->name, mac_string(netif->mac, mac));

    if (n == 0)
        netif_config(netif);

    return 0;
}

/* a frame the device received, the netbuf is the stack's from now on */
void netif_input(struct netif *netif, struct netbuf *nb)
{
    netif->stats.rx_packets++;
    netif->stats.rx_bytes += nb->len;
    nb->netif = netif;

    if (!(netif->flags & NETIF_UP)) {
        netif->stats.rx_dropped++;
        netbuf_free(nb);
        return;
    }

    ether_input(netif, nb);
}

/* send a frame, the netbuf stays the caller's */
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
