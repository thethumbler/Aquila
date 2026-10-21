/*
 * route: the default route
 *
 *   route add default gw <address>
 *   route del default
 */

#include <aqbox.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

static int usage(void)
{
    fprintf(stderr, "usage: route add default gw <address>\n"
                    "       route del default\n");
    return 1;
}

AQBOX_APPLET(route)(int argc, char **argv)
{
    struct rtentry rt;

    memset(&rt, 0, sizeof(rt));
    rt.rt_dst.sin_family = AF_INET;
    rt.rt_genmask.sin_family = AF_INET;

    unsigned long request;

    if (argc == 5 && !strcmp(argv[1], "add") && !strcmp(argv[2], "default") &&
        !strcmp(argv[3], "gw")) {
        rt.rt_gateway.sin_family = AF_INET;

        if (!inet_aton(argv[4], &rt.rt_gateway.sin_addr)) {
            fprintf(stderr, "route: %s: not an address\n", argv[4]);
            return 1;
        }

        rt.rt_flags = RTF_UP | RTF_GATEWAY;
        request = SIOCADDRT;
    } else if (argc == 3 && !strcmp(argv[1], "del") && !strcmp(argv[2], "default")) {
        request = SIOCDELRT;
    } else {
        return usage();
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0 || ioctl(fd, request, &rt) < 0) {
        perror("route");
        return 1;
    }

    return 0;
}
