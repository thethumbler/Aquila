/** \file
 * The interfaces' requests (ioctl() on an AF_INET socket): their list,
 * flags, addresses and hardware address, and the default route. Setting
 * is root's.
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/uaccess.h>

#include <aquila/errno.h>
#include <aquila/if.h>
#include <aquila/in.h>
#include <aquila/ioctl.h>
#include <aquila/route.h>

static int netif_index(struct netif *netif)
{
    int i = 1;

    for (struct netif *n = netifs; n && n != netif; n = n->next)
        ++i;

    return i;
}

static void ifaddr_put(struct sockaddr *a, uint32_t ip)
{
    struct sockaddr_in *sin = (struct sockaddr_in *) a;

    memset(a, 0, sizeof(*a));
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = ip;
}

static int ifaddr_get(const struct sockaddr *a, uint32_t *ip)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *) a;

    if (sin->sin_family != AF_INET)
        return -EAFNOSUPPORT;

    *ip = sin->sin_addr.s_addr;
    return 0;
}

/** SIOCGIFCONF: as many interfaces as there is room for (how much is
 * needed, without a buffer) */
static int ifconf(struct ifconf *uifc)
{
    struct ifconf ifc;

    if (copyin(&ifc, uifc, sizeof(ifc)))
        return -EFAULT;

    int n = 0;

    for (struct netif *netif = netifs; netif; netif = netif->next)
        ++n;

    if (!ifc.ifc_buf) {
        ifc.ifc_len = n * sizeof(struct ifreq);
        return copyout(uifc, &ifc, sizeof(ifc));
    }

    if (ifc.ifc_len < 0)
        return -EINVAL;

    int len = 0;

    /* each made here, then out */
    for (struct netif *netif = netifs; netif; netif = netif->next) {
        if (len + (int) sizeof(struct ifreq) > ifc.ifc_len)
            break;

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        memcpy(ifr.ifr_name, netif->name, sizeof(netif->name));
        ifaddr_put(&ifr.ifr_addr, netif->ip);

        if (copyout(ifc.ifc_buf + len, &ifr, sizeof(ifr)))
            return -EFAULT;

        len += sizeof(struct ifreq);
    }

    ifc.ifc_len = len;
    return copyout(uifc, &ifc, sizeof(ifc));
}

/** SIOCADDRT, SIOCDELRT: the default route (0.0.0.0/0 through a gateway),
 * to the interface on whose link the gateway is */
static int route(int request, struct rtentry *urt)
{
    struct rtentry _rt, *rt = &_rt;

    if (copyin(rt, urt, sizeof(*rt)))
        return -EFAULT;

    const struct sockaddr_in *dst = (const struct sockaddr_in *) &rt->rt_dst;
    const struct sockaddr_in *mask = (const struct sockaddr_in *) &rt->rt_genmask;
    const struct sockaddr_in *gateway = (const struct sockaddr_in *) &rt->rt_gateway;

    if (dst->sin_addr.s_addr || mask->sin_addr.s_addr)
        return -EINVAL;

    if (request == SIOCDELRT) {
        for (struct netif *netif = netifs; netif; netif = netif->next)
            netif->gateway = 0;

        return 0;
    }

    uint32_t gw = gateway->sin_addr.s_addr;

    if (!(rt->rt_flags & RTF_GATEWAY) || gateway->sin_family != AF_INET || !gw)
        return -EINVAL;

    for (struct netif *netif = netifs; netif; netif = netif->next) {
        if (netif->ip && (gw & netif->netmask) == (netif->ip & netif->netmask)) {
            netif->gateway = gw;
            return 0;
        }
    }

    return -ENETUNREACH;
}

static int ifreq_request(int request, struct ifreq *ifr);

int inet_ifioctl(int request, void *argp)
{
    switch (request) {
        case SIOCGIFCONF:
            return ifconf(argp);

        case SIOCADDRT:
        case SIOCDELRT:
            if (curproc->uid)
                return -EPERM;

            return route(request, argp);
    }

    /* the requests of an interface, by its name: the kernel's copy of
     * the ifreq, back out once done */
    struct ifreq ifr;

    switch (request) {
        case SIOCGIFFLAGS: case SIOCSIFFLAGS:
        case SIOCGIFADDR: case SIOCSIFADDR:
        case SIOCGIFNETMASK: case SIOCSIFNETMASK:
        case SIOCGIFBRDADDR: case SIOCGIFHWADDR:
        case SIOCGIFMTU: case SIOCGIFINDEX:
            break;
        default:
            return -ENOTTY;
    }

    if (copyin(&ifr, argp, sizeof(ifr)))
        return -EFAULT;

    ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

    int err = ifreq_request(request, &ifr);
    return err ? err : copyout(argp, &ifr, sizeof(ifr));
}

static int ifreq_request(int request, struct ifreq *ifr)
{
    struct netif *netif = netif_find(ifr->ifr_name);

    if (!netif)
        return -ENODEV;

    int set = request == SIOCSIFFLAGS || request == SIOCSIFADDR || request == SIOCSIFNETMASK;

    if (set && curproc->uid)
        return -EPERM;

    uint32_t ip;
    int err;

    switch (request) {
        case SIOCGIFFLAGS:
            ifr->ifr_flags = (netif->flags & NETIF_LOOPBACK ? IFF_LOOPBACK : IFF_BROADCAST | IFF_MULTICAST) |
                (netif->flags & NETIF_UP ? IFF_UP : 0) |
                (netif->flags & NETIF_RUNNING ? IFF_RUNNING : 0);
            return 0;

        case SIOCSIFFLAGS:
            if (ifr->ifr_flags & IFF_UP)
                netif->flags |= NETIF_UP;
            else
                netif->flags &= ~NETIF_UP;
            return 0;

        case SIOCGIFADDR:
            ifaddr_put(&ifr->ifr_addr, netif->ip);
            return 0;

        case SIOCSIFADDR:
            if ((err = ifaddr_get(&ifr->ifr_addr, &ip)))
                return err;

            netif->ip = ip;

            /* the class's mask until one is set */
            if (!netif->netmask)
                netif->netmask = htonl(0xffffff00);

            return 0;

        case SIOCGIFNETMASK:
            ifaddr_put(&ifr->ifr_netmask, netif->netmask);
            return 0;

        case SIOCSIFNETMASK:
            if ((err = ifaddr_get(&ifr->ifr_netmask, &ip)))
                return err;

            netif->netmask = ip;
            return 0;

        case SIOCGIFBRDADDR:
            ifaddr_put(&ifr->ifr_broadaddr, netif->ip | ~netif->netmask);
            return 0;

        case SIOCGIFHWADDR:
            memset(&ifr->ifr_hwaddr, 0, sizeof(ifr->ifr_hwaddr));
            ifr->ifr_hwaddr.sa_family = netif->flags & NETIF_LOOPBACK ? ARPHRD_LOOPBACK : ARPHRD_ETHER;
            memcpy(ifr->ifr_hwaddr.sa_data, netif->mac, 6);
            return 0;

        case SIOCGIFMTU:
            ifr->ifr_mtu = netif->mtu;
            return 0;

        case SIOCGIFINDEX:
            ifr->ifr_ifindex = netif_index(netif);
            return 0;
    }

    return -ENOTTY;
}
