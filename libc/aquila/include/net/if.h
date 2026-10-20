#ifndef _NET_IF_H
#define _NET_IF_H

/* network interfaces: there are none yet (no SIOCGIFCONF, no names) */

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ    16
#define IF_NAMESIZE IFNAMSIZ

#define IFF_UP          0x0001
#define IFF_BROADCAST   0x0002
#define IFF_LOOPBACK    0x0008
#define IFF_POINTOPOINT 0x0010
#define IFF_RUNNING     0x0040
#define IFF_MULTICAST   0x1000

/* an address of an interface, as large as an IPv4 one */
struct __ifaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct __ifaddr ifru_addr;
        struct __ifaddr ifru_dstaddr;
        struct __ifaddr ifru_broadaddr;
        struct __ifaddr ifru_netmask;
        short ifru_flags;
        int   ifru_ifindex;
        int   ifru_mtu;
    } ifr_ifru;
};
#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_ifindex   ifr_ifru.ifru_ifindex
#define ifr_mtu       ifr_ifru.ifru_mtu

struct ifconf {
    int ifc_len;
    union {
        char *ifcu_buf;
        struct ifreq *ifcu_req;
    } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

struct if_nameindex {
    unsigned int if_index;
    char *if_name;
};

unsigned int if_nametoindex(const char *name);
char *if_indextoname(unsigned int index, char *name);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ! _NET_IF_H */
