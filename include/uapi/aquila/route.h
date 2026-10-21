#ifndef _AQUILA_ROUTE_H
#define _AQUILA_ROUTE_H

/* routes (SIOCADDRT, SIOCDELRT), shared by the kernel and <net/route.h>:
 * there is the default one (a gateway, to 0.0.0.0/0) */

#include <aquila/in.h>

/* the addresses are struct sockaddr_in, as struct sockaddr (as Linux has
 * them) */
struct rtentry {
    struct sockaddr rt_dst;
    struct sockaddr rt_gateway;
    struct sockaddr rt_genmask;
    unsigned short rt_flags;
    short rt_metric;
    char *rt_dev;
};

#define RTF_UP          0x0001
#define RTF_GATEWAY     0x0002
#define RTF_HOST        0x0004

#endif /* ! _AQUILA_ROUTE_H */
