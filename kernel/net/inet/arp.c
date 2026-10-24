/** \file
 * ARP: finding the hardware (MAC) address of an IPv4 address on the link.
 *
 * ARP answers the requests for our own address, and keeps a table of the
 * other machines' hardware addresses, learned from their requests and
 * replies and from the IPv4 packets they send us. A packet to an address
 * that isn't in the table waits (up to `ARP_HOLD` packets per address)
 * while ARP asks for it, `ARP_TRIES` times, a second apart. An entry that
 * hasn't been heard from for `ARP_EXPIRE` is asked for again: the machine
 * may have gone, or another may have taken its address.
 */

#include <core/system.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <sys/callout.h>
#include <core/arch.h>

#include <aquila/errno.h>

#define ARP_ENTRIES 32

/* how long an entry is good for after it was last heard from, as on the
 * BSDs */
#define ARP_EXPIRE  (20 * 60 * 1000ULL)     /* ms */

/* The table: an IPv4 address's hardware address, on an interface. */
static struct arp_entry {
    struct netif *netif;
    uint32_t ip;
    uint8_t mac[6];
    uint32_t used;      /* when it was last updated (clock), to replace the oldest */
    uint64_t heard;     /* the same, in ms since boot, to expire it */
} table[ARP_ENTRIES];

static uint32_t clock;

#define ARP_PENDING 8
#define ARP_HOLD    3
#define ARP_TRIES   3

/* The addresses being asked for, with the packets waiting for them. */
static struct arp_pending {
    struct netif *netif;
    uint32_t ip;
    struct netbuf *held[ARP_HOLD];
    int nheld;
    int tries;
    struct callout retry;
} pending[ARP_PENDING];

static const uint8_t ether_broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/** Broadcasts a request: who has `ip`? */
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

/** Gives up on an address being asked for: its waiting packets are
 * dropped. */
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

/** Asks again for an address that hasn't answered, a second after the
 * last request, or gives up after `ARP_TRIES` requests. */
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

/**
 * Holds a packet until the hardware address of its next hop is known, and
 * asks for it. If too many packets wait for that address, the oldest is
 * dropped. The netbuf is ARP's from now on.
 *
 * @param netif  the interface to send it on
 * @param ip     the next hop's address
 * @param nb     the IPv4 packet
 * @return 0, or `-ENOBUFS` if too many addresses are being asked for
 *         already (the packet is dropped)
 */
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

    /* drop the oldest packet if too many wait */
    if (p->nheld == ARP_HOLD) {
        netif->stats.tx_dropped++;
        netbuf_free(p->held[0]);
        memmove(&p->held[0], &p->held[1], (ARP_HOLD - 1) * sizeof(p->held[0]));
        p->nheld--;
    }

    p->held[p->nheld++] = nb;
    return 0;
}

/** Sends the packets that waited for an address, now that it is known. */
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

/**
 * Records an address's hardware address, replacing the oldest entry if
 * the table is full, and sends the packets that waited for it.
 *
 * @param netif  the interface it was heard on
 * @param ip     the IPv4 address
 * @param mac    its hardware address
 */
void arp_update(struct netif *netif, uint32_t ip, const uint8_t mac[6])
{
    struct arp_entry *slot = &table[0];

    for (int i = 0; i < ARP_ENTRIES; ++i) {
        if (table[i].netif == netif && table[i].ip == ip) {
            slot = &table[i];
            break;
        }

        /* otherwise, use the oldest entry */
        if (table[i].used < slot->used)
            slot = &table[i];
    }

    slot->netif = netif;
    slot->ip    = ip;
    slot->used  = ++clock;
    slot->heard = arch_rtime_ms();
    memcpy(slot->mac, mac, 6);

    pending_flush(netif, ip, mac);
}

/**
 * Looks up an address's hardware address. An entry that has expired is
 * dropped, so that the address is asked for again.
 *
 * @param netif  the interface
 * @param ip     the IPv4 address
 * @param mac    set to the hardware address, if it is known
 * @return 0 if it is known, or -1
 */
int arp_lookup(struct netif *netif, uint32_t ip, uint8_t mac[6])
{
    for (int i = 0; i < ARP_ENTRIES; ++i) {
        if (table[i].netif == netif && table[i].ip == ip) {
            /* too old: drop the entry, so that the address is asked for
             * again */
            if (arch_rtime_ms() - table[i].heard > ARP_EXPIRE) {
                table[i].netif = NULL;
                return -1;
            }

            memcpy(mac, table[i].mac, 6);
            return 0;
        }
    }

    return -1;
}

/**
 * Takes in an ARP packet: the sender's address is recorded, and a request
 * for our address is answered.
 *
 * @param netif  the interface it came in on
 * @param nb     the packet; it is freed or turned into the reply
 */
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

    /* a request for our address: turn it into the reply */
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
