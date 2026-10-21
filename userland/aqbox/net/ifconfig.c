/*
 * ifconfig: the network interfaces, and their configuration
 *
 *   ifconfig [-a]                      all of them
 *   ifconfig <interface>               one
 *   ifconfig <interface> [<address>] [netmask <mask>] [up|down]
 */

#include <aqbox.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *addr_of(struct sockaddr *a)
{
    return inet_ntoa(((struct sockaddr_in *) a)->sin_addr);
}

static int show(int fd, const char *name)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        perror(name);
        return 1;
    }

    short flags = ifr.ifr_flags;
    ioctl(fd, SIOCGIFMTU, &ifr);

    printf("%s: flags=%d<%s%s%s%s> mtu %d\n", name, flags,
           flags & IFF_UP ? "UP," : "",
           flags & IFF_BROADCAST ? "BROADCAST," : "",
           flags & IFF_LOOPBACK ? "LOOPBACK," : "",
           flags & IFF_RUNNING ? "RUNNING" : "", ifr.ifr_mtu);

    char addr[16], mask[16], bcast[16];

    ioctl(fd, SIOCGIFADDR, &ifr);
    strcpy(addr, addr_of(&ifr.ifr_addr));
    ioctl(fd, SIOCGIFNETMASK, &ifr);
    strcpy(mask, addr_of(&ifr.ifr_netmask));
    ioctl(fd, SIOCGIFBRDADDR, &ifr);
    strcpy(bcast, addr_of(&ifr.ifr_broadaddr));

    if (flags & IFF_LOOPBACK) {
        printf("        inet %s  netmask %s\n        loop\n", addr, mask);
        return 0;
    }

    printf("        inet %s  netmask %s  broadcast %s\n", addr, mask, bcast);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        unsigned char *m = (unsigned char *) ifr.ifr_hwaddr.sa_data;
        printf("        ether %02x:%02x:%02x:%02x:%02x:%02x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    }

    return 0;
}

static int set_addr(int fd, const char *name, unsigned long request, const char *text)
{
    struct ifreq ifr;
    struct sockaddr_in *sin = (struct sockaddr_in *) &ifr.ifr_addr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin->sin_family = AF_INET;

    if (!inet_aton(text, &sin->sin_addr)) {
        fprintf(stderr, "ifconfig: %s: not an address\n", text);
        return 1;
    }

    if (ioctl(fd, request, &ifr) < 0) {
        perror("ifconfig");
        return 1;
    }

    return 0;
}

static int set_up(int fd, const char *name, int up)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0)
        goto err;

    if (up)
        ifr.ifr_flags |= IFF_UP;
    else
        ifr.ifr_flags &= ~IFF_UP;

    if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0)
        goto err;

    return 0;

err:
    perror("ifconfig");
    return 1;
}

AQBOX_APPLET(ifconfig)(int argc, char **argv)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("ifconfig: socket");
        return 1;
    }

    if (argc < 2 || !strcmp(argv[1], "-a")) {
        struct ifreq reqs[16];
        struct ifconf ifc = {sizeof(reqs), {(char *) reqs}};

        if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
            perror("ifconfig: SIOCGIFCONF");
            return 1;
        }

        int ret = 0;
        for (size_t i = 0; i < ifc.ifc_len / sizeof(struct ifreq); ++i)
            ret |= show(fd, reqs[i].ifr_name);

        return ret;
    }

    const char *name = argv[1];

    if (argc == 2)
        return show(fd, name);

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "up")) {
            if (set_up(fd, name, 1))
                return 1;
        } else if (!strcmp(argv[i], "down")) {
            if (set_up(fd, name, 0))
                return 1;
        } else if (!strcmp(argv[i], "netmask") && i + 1 < argc) {
            if (set_addr(fd, name, SIOCSIFNETMASK, argv[++i]))
                return 1;
        } else if (set_addr(fd, name, SIOCSIFADDR, argv[i])) {
            return 1;
        }
    }

    return 0;
}
