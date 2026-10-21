#ifndef _NETINET_IP_H
#define _NETINET_IP_H

/* IPv4's socket options (IPPROTO_IP), and the type of service values;
 * the kernel takes none of the options yet (ENOPROTOOPT) */

#include <netinet/in.h>

#define IP_TOS          1
#define IP_TTL          2
#define IP_MINTTL       21

#define IPTOS_LOWDELAY          0x10
#define IPTOS_THROUGHPUT        0x08
#define IPTOS_RELIABILITY       0x04
#define IPTOS_LOWCOST           0x02
#define IPTOS_MINCOST           IPTOS_LOWCOST

#define IPTOS_PREC_NETCONTROL       0xe0
#define IPTOS_PREC_INTERNETCONTROL  0xc0
#define IPTOS_PREC_CRITIC_ECP       0xa0
#define IPTOS_PREC_FLASHOVERRIDE    0x80
#define IPTOS_PREC_FLASH            0x60
#define IPTOS_PREC_IMMEDIATE        0x40
#define IPTOS_PREC_PRIORITY         0x20
#define IPTOS_PREC_ROUTINE          0x00

#define IPTOS_DSCP_CS0  0x00
#define IPTOS_DSCP_CS1  0x20
#define IPTOS_DSCP_CS2  0x40
#define IPTOS_DSCP_CS3  0x60
#define IPTOS_DSCP_CS4  0x80
#define IPTOS_DSCP_CS5  0xa0
#define IPTOS_DSCP_CS6  0xc0
#define IPTOS_DSCP_CS7  0xe0
#define IPTOS_DSCP_AF11 0x28
#define IPTOS_DSCP_AF12 0x30
#define IPTOS_DSCP_AF13 0x38
#define IPTOS_DSCP_AF21 0x48
#define IPTOS_DSCP_AF22 0x50
#define IPTOS_DSCP_AF23 0x58
#define IPTOS_DSCP_AF31 0x68
#define IPTOS_DSCP_AF32 0x70
#define IPTOS_DSCP_AF33 0x78
#define IPTOS_DSCP_AF41 0x88
#define IPTOS_DSCP_AF42 0x90
#define IPTOS_DSCP_AF43 0x98
#define IPTOS_DSCP_VA   0xb0
#define IPTOS_DSCP_EF   0xb8

#endif /* ! _NETINET_IP_H */
