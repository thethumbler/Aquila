/*
 * ARP: answers who has our address, keeps the hardware addresses of the
 * others (from their requests and replies, and the IPv4 packets they send
 * us), and asks for those packets are sent to: they wait for the answer
 * (ARP_HOLD of them an address), the question is asked ARP_TRIES times a
 * second apart
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <sys/callout.h>

#include <aquila/errno.h>

#define ARP_ENTRIES 32

static struct arp_entry {
    struct netif *netif;
    uint32_t ip;
    uint8_t mac[6];
    uint32_t used;      /* when it was last updated, for replacing */
} table[ARP_ENTRIES];

static uint32_t clock;

#define ARP_PENDING 8
#define ARP_HOLD    3
#define ARP_TRIES   3

/* addresses asked for, with the packets waiting for them */
static struct arp_pending {
    struct netif *netif;
    uint32_t ip;
    struct netbuf *held[ARP_HOLD];
    int nheld;
    int tries;
    struct callout retry;
} pending[ARP_PENDING];

static const uint8_t ether_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* who has `ip' */
static void arp_request(struct netif *netif, uint32_t ip)
{
    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return;

    struct arp_hdr *ah = netbuf_put(nb, sizeof(*ah));

    ah->htype = htons(1);
    ah->ptype = htons(ETHER_TYPE_IPV4);
    ah->hlen  = 6;
    ah->plen  = 4;
    ah->op    = htons(1);
    memcpy(ah->sha, netif->mac, 6);
    ah->spa   = netif->ip;
    memset(ah->tha, 0, 6);
    ah->tpa   = ip;

    ether_output(netif, ether_broadcast, ETHER_TYPE_ARP, nb);
}

static void pending_drop(struct arp_pending *p)
{
    for (int i = 0; i < p->nheld; ++i) {
        p->netif->stats.tx_dropped++;
        netbuf_free(p->held[i]);
    }

    callout_stop(&p->retry);
    p->netif = NULL;
    p->nheld = 0;
}

/* no answer yet: again, or the packets go */
static void arp_retry(void *arg)
{
    struct arp_pending *p = arg;

    if (++p->tries >= ARP_TRIES) {
        pending_drop(p);
        return;
    }

    arp_request(p->netif, p->ip);
    net_callout_set(&p->retry, 1000000000ULL, arp_retry, p);
}

/* the packet waits for the hardware address of `ip' (asked for), the
 * netbuf is ARP's */
int arp_resolve(struct netif *netif, uint32_t ip, struct netbuf *nb)
{
    struct arp_pending *p = NULL, *free = NULL;

    for (int i = 0; i < ARP_PENDING; ++i) {
        if (pending[i].netif == netif && pending[i].ip == ip)
            p = &pending[i];
        else if (!pending[i].netif && !free)
            free = &pending[i];
    }

    if (!p) {
        if (!free) {
            netif->stats.tx_dropped++;
            netbuf_free(nb);
            return -ENOBUFS;
        }

        p = free;
        p->netif = netif;
        p->ip    = ip;
        p->nheld = 0;
        p->tries = 0;

        arp_request(netif, ip);
        net_callout_set(&p->retry, 1000000000ULL, arp_retry, p);
    }

    /* the oldest goes if there are too many */
    if (p->nheld == ARP_HOLD) {
        netif->stats.tx_dropped++;
        netbuf_free(p->held[0]);
        memmove(&p->held[0], &p->held[1], (ARP_HOLD - 1) * sizeof(p->held[0]));
        p->nheld--;
    }

    p->held[p->nheld++] = nb;
    return 0;
}

/* the address is known: what waited for it goes */
static void pending_flush(struct netif *netif, uint32_t ip, const uint8_t mac[6])
{
    for (int i = 0; i < ARP_PENDING; ++i) {
        struct arp_pending *p = &pending[i];

        if (p->netif != netif || p->ip != ip)
            continue;

        callout_stop(&p->retry);
        p->netif = NULL;

        for (int j = 0; j < p->nheld; ++j)
            ether_output(netif, mac, ETHER_TYPE_IPV4, p->held[j]);

        p->nheld = 0;
    }
}

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

    pending_flush(netif, ip, mac);
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
