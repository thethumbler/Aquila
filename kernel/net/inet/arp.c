/*
 * ARP: answers who has our address, and keeps the hardware addresses of
 * the others (from their requests and replies, and the IPv4 packets they
 * send us)
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>

#define ARP_ENTRIES 32

static struct arp_entry {
    struct netif *netif;
    uint32_t ip;
    uint8_t mac[6];
    uint32_t used;      /* when it was last updated, for replacing */
} table[ARP_ENTRIES];

static uint32_t clock;

void arp_update(struct netif *netif, uint32_t ip, const uint8_t mac[6])
{
    struct arp_entry *slot = &table[0];

    for (int i = 0; i < ARP_ENTRIES; ++i) {
        if (table[i].netif == netif && table[i].ip == ip) {
            slot = &table[i];
            break;
        }

        /* else the oldest */
        if (table[i].used < slot->used)
            slot = &table[i];
    }

    slot->netif = netif;
    slot->ip    = ip;
    slot->used  = ++clock;
    memcpy(slot->mac, mac, 6);
}

int arp_lookup(struct netif *netif, uint32_t ip, uint8_t mac[6])
{
    for (int i = 0; i < ARP_ENTRIES; ++i) {
        if (table[i].netif == netif && table[i].ip == ip) {
            memcpy(mac, table[i].mac, 6);
            return 0;
        }
    }

    return -1;
}

void arp_input(struct netif *netif, struct netbuf *nb)
{
    struct arp_hdr *ah = (struct arp_hdr *) nb->data;

    if (nb->len < sizeof(*ah) || ntohs(ah->htype) != 1 ||
        ntohs(ah->ptype) != ETHER_TYPE_IPV4 || ah->hlen != 6 || ah->plen != 4) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    if (ah->spa)
        arp_update(netif, ah->spa, ah->sha);

    /* who has our address: we have, the request turned into the reply */
    if (ntohs(ah->op) == 1 && netif->ip && ah->tpa == netif->ip) {
        uint8_t requester[6];
        memcpy(requester, ah->sha, 6);

        ah->op = htons(2);
        memcpy(ah->tha, ah->sha, 6);
        ah->tpa = ah->spa;
        memcpy(ah->sha, netif->mac, 6);
        ah->spa = netif->ip;

        nb->len = sizeof(*ah);
        ether_output(netif, requester, ETHER_TYPE_ARP, nb);
        return;
    }

    netbuf_free(nb);
}
