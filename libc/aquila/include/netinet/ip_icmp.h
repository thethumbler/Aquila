#ifndef _NETINET_IP_ICMP_H
#define _NETINET_IP_ICMP_H

/* ICMP's header (Linux's struct icmphdr) */

#include <stdint.h>

struct icmphdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t sequence;
        } echo;
        uint32_t gateway;
    } un;
};

#define ICMP_ECHOREPLY      0
#define ICMP_DEST_UNREACH   3
#define ICMP_ECHO           8
#define ICMP_TIME_EXCEEDED  11

#endif /* ! _NETINET_IP_ICMP_H */
