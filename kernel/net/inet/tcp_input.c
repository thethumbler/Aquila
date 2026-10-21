/*
 * TCP's input (RFC 9293 3.10.7, "SEGMENT ARRIVES"): a segment goes to
 * its connection's state machine, or to a listener, which makes a
 * connection for a SYN; one for no connection is answered with RST.
 *
 * Acknowledgments drive the congestion window (RFC 5681: slow start,
 * congestion avoidance; three duplicate ACKs retransmit and recover, RFC
 * 6582's NewReno). Data out of order waits in a list, sorted, until what
 * is missing comes; a duplicate ACK tells the peer.
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <net/netif.h>
#include <net/tcp.h>

#include <aquila/errno.h>

/* a segment, its header in our byte order */
struct seg {
    uint32_t seq, ack;
    uint16_t win;
    int flags;
    uint16_t mss;               /* its MSS option, 0 if none */
    uint8_t *data;
    uint32_t len;
};

static void options(const struct tcp_hdr *th, size_t hlen, struct seg *s)
{
    const uint8_t *o = (const uint8_t *) (th + 1);
    const uint8_t *end = (const uint8_t *) th + hlen;

    while (o < end) {
        if (*o == 0)            /* the end */
            break;

        if (*o == 1) {          /* no operation */
            ++o;
            continue;
        }

        if (end - o < 2 || o[1] < 2 || o[1] > end - o)
            break;

        if (o[0] == 2 && o[1] == 4)
            s->mss = o[2] << 8 | o[3];

        o += o[1];
    }
}

/* the segments' size: the peer's MSS (536 if it didn't say), not more
 * than ours */
static void set_mss(struct tcp_pcb *pcb, uint16_t peer)
{
    struct netif *netif = ipv4_route(pcb->raddr);
    uint16_t ours = netif ? netif->mtu - sizeof(struct ipv4_hdr) - sizeof(struct tcp_hdr) :
                    TCP_MSS_DEFAULT;

    pcb->mss = MIN(peer ? peer : TCP_MSS_DEFAULT, ours);
}

static void rtt_sample(struct tcp_pcb *pcb, uint32_t ack)
{
    if (pcb->rtt_timing && SEQ_GT(ack, pcb->rtt_seq)) {
        tcp_rtt_update(pcb, arch_rtime_ns() - pcb->rtt_start);
        pcb->rtt_timing = 0;
    }
}

/* a SYN for a listener: a connection, SYN_RECEIVED, its listener's until
 * accept() takes it */
static void listen_input(struct tcp_pcb *l, const struct ipv4_hdr *ih, const struct tcp_hdr *th,
                         struct seg *s, struct netif *netif)
{
    if (s->flags & TH_RST)
        return;

    if (s->flags & TH_ACK) {
        tcp_reply_rst(netif, ih, th, s->len);
        return;
    }

    /* full: the peer tries again */
    if (!(s->flags & TH_SYN) || l->qlen >= l->backlog)
        return;

    struct tcp_pcb *pcb = tcp_pcb_new();

    if (!pcb)
        return;

    pcb->laddr = ih->dst;
    pcb->lport = th->dst;
    pcb->raddr = ih->src;
    pcb->rport = th->src;
    pcb->state = TCP_SYN_RECEIVED;

    pcb->irs = s->seq;
    pcb->rcv_nxt = s->seq + 1;
    pcb->snd_wnd = s->win;
    pcb->snd_wl1 = s->seq;
    set_mss(pcb, s->mss);

    pcb->iss = tcp_iss(pcb);
    pcb->snd_una = pcb->iss;

    /* the listener's options */
    pcb->nodelay   = l->nodelay;
    pcb->keepalive = l->keepalive;

    if (tcp_insert(pcb)) {
        tcp_pcb_free(pcb);
        return;
    }

    pcb->parent = l;
    l->qlen++;

    tcp_send_syn(pcb);
}

/* RFC 9293 3.10.7.3 */
static void syn_sent_input(struct tcp_pcb *pcb, const struct ipv4_hdr *ih, const struct tcp_hdr *th,
                           struct seg *s, struct netif *netif)
{
    if (s->flags & TH_ACK) {
        if (SEQ_LEQ(s->ack, pcb->iss) || SEQ_GT(s->ack, pcb->snd_max)) {
            tcp_reply_rst(netif, ih, th, s->len);
            return;
        }
    }

    if (s->flags & TH_RST) {
        if (s->flags & TH_ACK)
            tcp_drop(pcb, ECONNREFUSED);
        return;
    }

    if (!(s->flags & TH_SYN))
        return;

    pcb->irs = s->seq;
    pcb->rcv_nxt = s->seq + 1;
    set_mss(pcb, s->mss);

    pcb->snd_wnd = s->win;
    pcb->snd_wl1 = s->seq;
    pcb->snd_wl2 = s->ack;

    if (!(s->flags & TH_ACK)) {
        /* both opened at once: SYN and ACK for theirs */
        pcb->state = TCP_SYN_RECEIVED;
        pcb->retries = 0;
        tcp_send_syn(pcb);
        return;
    }

    /* our SYN acked (data with it is left for the peer to send again) */
    if (!pcb->retries)
        rtt_sample(pcb, s->ack);

    pcb->snd_una = s->ack;
    pcb->retries = 0;
    callout_stop(&pcb->rexmt);

    tcp_established(pcb);
    tcp_send_ack(pcb);
}

/* RFC 9293 3.10.7.4: within what we said we'd take */
static int acceptable(struct tcp_pcb *pcb, struct seg *s, uint32_t wnd)
{
    uint32_t seglen = s->len + !!(s->flags & TH_SYN) + !!(s->flags & TH_FIN);
    uint32_t nxt = pcb->rcv_nxt;

#define IN_WINDOW(x)    (SEQ_GEQ(x, nxt) && SEQ_LT(x, nxt + wnd))

    if (!seglen)
        return wnd ? IN_WINDOW(s->seq) : s->seq == nxt;

    return wnd && (IN_WINDOW(s->seq) || IN_WINDOW(s->seq + seglen - 1));

#undef IN_WINDOW
}

/* the first segment not acked again (fast retransmit, a partial ACK) */
static void retransmit_first(struct tcp_pcb *pcb)
{
    uint32_t nxt = pcb->snd_nxt;

    pcb->snd_nxt = pcb->snd_una;
    pcb->rtt_timing = 0;

    uint32_t cwnd = pcb->cwnd;
    int nodelay = pcb->nodelay;

    /* one segment, whatever the windows and Nagle say */
    pcb->cwnd = pcb->mss;
    pcb->nodelay = 1;
    tcp_output(pcb);
    pcb->cwnd = cwnd;
    pcb->nodelay = nodelay;

    if (SEQ_GT(nxt, pcb->snd_nxt))
        pcb->snd_nxt = nxt;
}

/* an ACK for what we sent; 0 if the segment goes on */
static int ack_input(struct tcp_pcb *pcb, struct seg *s)
{
    if (SEQ_GT(s->ack, pcb->snd_max)) {
        /* for what we didn't send */
        tcp_send_ack(pcb);
        return -1;
    }

    if (SEQ_GT(s->ack, pcb->snd_una)) {
        uint32_t acked = s->ack - pcb->snd_una;
        uint32_t buffered = ringbuf_available(pcb->sndbuf);
        int fin_acked = pcb->fin_queued && acked > buffered;

        /* the data acked goes (the FIN is after it) */
        ringbuf_discard(pcb->sndbuf, MIN(acked, buffered));

        pcb->snd_una = s->ack;

        if (SEQ_LT(pcb->snd_nxt, pcb->snd_una))
            pcb->snd_nxt = pcb->snd_una;

        rtt_sample(pcb, s->ack);
        pcb->retries = 0;

        if (pcb->in_recovery) {
            if (SEQ_GEQ(s->ack, pcb->recover)) {
                /* all that was out when it was lost: recovered */
                pcb->in_recovery = 0;
                pcb->cwnd = pcb->ssthresh;
            } else {
                /* a partial ACK: the next hole */
                retransmit_first(pcb);
                pcb->cwnd = pcb->cwnd > acked ? pcb->cwnd - acked + pcb->mss : pcb->mss;
            }
        } else if (pcb->cwnd < pcb->ssthresh) {
            pcb->cwnd += MIN(acked, (uint32_t) pcb->mss);   /* slow start */
        } else {
            pcb->cwnd += MAX(1U, (uint32_t) pcb->mss * pcb->mss / pcb->cwnd);
        }

        pcb->dupacks = 0;

        /* the timer for what is still out */
        callout_stop(&pcb->rexmt);
        pcb->persist = 0;

        if (pcb->snd_una != pcb->snd_max)
            tcp_rexmt_start(pcb);

        tcp_wakeup(pcb);

        if (fin_acked) {
            switch (pcb->state) {
                case TCP_FIN_WAIT_1:
                    pcb->state = TCP_FIN_WAIT_2;
                    tcp_fin_wait_2(pcb);
                    break;
                case TCP_CLOSING:
                    tcp_time_wait(pcb);
                    break;
                case TCP_LAST_ACK:
                    tcp_closed(pcb);
                    return -1;
            }
        }
    } else if (s->ack == pcb->snd_una && !s->len && !(s->flags & TH_FIN) &&
               s->win == pcb->snd_wnd && pcb->snd_una != pcb->snd_max) {
        /* a duplicate ACK: something was lost, what follows got there */
        if (++pcb->dupacks == 3 && !pcb->in_recovery) {
            uint32_t flight = pcb->snd_max - pcb->snd_una;

            pcb->ssthresh = MAX(flight / 2, 2U * pcb->mss);
            pcb->recover = pcb->snd_max;
            pcb->in_recovery = 1;

            retransmit_first(pcb);
            pcb->cwnd = pcb->ssthresh + 3 * pcb->mss;
        } else if (pcb->dupacks > 3 && pcb->in_recovery) {
            pcb->cwnd += pcb->mss;
            tcp_output(pcb);
        }
    }

    /* the peer's window, from its latest segment */
    if (SEQ_LT(pcb->snd_wl1, s->seq) || (pcb->snd_wl1 == s->seq && SEQ_LEQ(pcb->snd_wl2, s->ack))) {
        int opened = !pcb->snd_wnd && s->win;

        pcb->snd_wnd = s->win;
        pcb->snd_wl1 = s->seq;
        pcb->snd_wl2 = s->ack;

        if (opened && pcb->persist) {
            pcb->persist = 0;
            callout_stop(&pcb->rexmt);
            pcb->snd_nxt = pcb->snd_una;
        }
    }

    return 0;
}

/* a segment ahead of rcv_nxt waits, sorted (it is its netbuf, its data) */
static void ooo_insert(struct tcp_pcb *pcb, struct netbuf *nb, struct seg *s, int fin)
{
    nb->data = s->data;
    nb->len  = s->len;
    nb->seq  = s->seq;
    nb->fin  = fin;

    struct netbuf **p = &pcb->ooo;

    while (*p && SEQ_LT((*p)->seq, s->seq))
        p = &(*p)->next;

    /* the same again */
    if (*p && (*p)->seq == s->seq && (*p)->len >= s->len) {
        netbuf_free(nb);
        return;
    }

    nb->next = *p;
    *p = nb;
}

/* what waited and now follows rcv_nxt; 1 if the FIN came with it */
static int ooo_drain(struct tcp_pcb *pcb)
{
    int fin = 0;

    while (pcb->ooo && SEQ_LEQ(pcb->ooo->seq, pcb->rcv_nxt)) {
        struct netbuf *nb = pcb->ooo;
        uint32_t skip = pcb->rcv_nxt - nb->seq;

        pcb->ooo = nb->next;

        if (skip < nb->len) {
            uint32_t n = ringbuf_write(pcb->rcvbuf, nb->len - skip, (char *) nb->data + skip);

            pcb->rcv_nxt += n;

            if (n < nb->len - skip)
                nb->fin = 0;
        }

        if (nb->fin && nb->seq + nb->len == pcb->rcv_nxt)
            fin = 1;

        netbuf_free(nb);

        if (fin)
            break;
    }

    return fin;
}

/* the peer's FIN, in its place: the end of the data */
static void fin_input(struct tcp_pcb *pcb)
{
    pcb->rcv_nxt++;
    pcb->fin_received = 1;
    tcp_send_ack(pcb);
    tcp_wakeup(pcb);

    switch (pcb->state) {
        case TCP_SYN_RECEIVED:
        case TCP_ESTABLISHED:
            pcb->state = TCP_CLOSE_WAIT;
            break;
        case TCP_FIN_WAIT_1:
            /* ours not acked yet */
            pcb->state = TCP_CLOSING;
            break;
        case TCP_FIN_WAIT_2:
        case TCP_TIME_WAIT:
            tcp_time_wait(pcb);
            break;
    }
}

/* the synchronized states (RFC 9293 3.10.7.4); the netbuf is freed or
 * kept */
static void segment_input(struct tcp_pcb *pcb, struct netbuf *nb, struct seg *s,
                          const struct ipv4_hdr *ih, const struct tcp_hdr *th, struct netif *netif)
{
    uint32_t wnd = SEQ_GT(pcb->rcv_adv, pcb->rcv_nxt) ? pcb->rcv_adv - pcb->rcv_nxt : 0;

    if (!acceptable(pcb, s, wnd)) {
        if (!(s->flags & TH_RST))
            tcp_send_ack(pcb);
        goto done;
    }

    /* RFC 5961: a reset only at rcv_nxt, else a challenge ACK */
    if (s->flags & TH_RST) {
        if (s->seq != pcb->rcv_nxt) {
            tcp_send_ack(pcb);
            goto done;
        }

        switch (pcb->state) {
            case TCP_SYN_RECEIVED:
                tcp_drop(pcb, ECONNREFUSED);
                break;
            case TCP_CLOSING:
            case TCP_LAST_ACK:
            case TCP_TIME_WAIT:
                tcp_closed(pcb);
                break;
            default:
                tcp_drop(pcb, ECONNRESET);
        }

        goto done;
    }

    /* a SYN in a connection: a challenge ACK (RFC 5961) */
    if (s->flags & TH_SYN) {
        tcp_send_ack(pcb);
        goto done;
    }

    if (!(s->flags & TH_ACK))
        goto done;

    tcp_keepalive_reset(pcb);

    /* the data to what we take: from rcv_nxt, in the window */
    int fin = !!(s->flags & TH_FIN);

    if (SEQ_LT(s->seq, pcb->rcv_nxt)) {
        uint32_t skip = pcb->rcv_nxt - s->seq;

        if (skip > s->len) {
            skip = s->len;
            fin = 0;
        }

        s->data += skip;
        s->len  -= skip;
        s->seq  += skip;
    }

    if (SEQ_GT(s->seq + s->len, pcb->rcv_nxt + wnd)) {
        s->len = SEQ_GT(pcb->rcv_nxt + wnd, s->seq) ? pcb->rcv_nxt + wnd - s->seq : 0;
        fin = 0;
    }

    if (pcb->state == TCP_SYN_RECEIVED) {
        if (SEQ_LEQ(s->ack, pcb->snd_una) || SEQ_GT(s->ack, pcb->snd_max)) {
            tcp_reply_rst(netif, ih, th, s->len);
            goto done;
        }

        if (!pcb->retries)
            rtt_sample(pcb, s->ack);

        pcb->snd_una = s->ack;
        pcb->snd_wnd = s->win;
        pcb->snd_wl1 = s->seq;
        pcb->snd_wl2 = s->ack;
        pcb->retries = 0;
        callout_stop(&pcb->rexmt);

        tcp_established(pcb);
    } else if (ack_input(pcb, s)) {
        goto done;
    }

    int fin_now = 0;

    /* the data, in the states it may come in */
    if ((s->len || fin) && (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_FIN_WAIT_1 ||
                            pcb->state == TCP_FIN_WAIT_2)) {
        /* nobody reads it anymore: RST (RFC 2525) */
        if (s->len && pcb->detached) {
            tcp_send_rst(pcb);
            tcp_drop(pcb, 0);
            goto done;
        }

        if (s->seq == pcb->rcv_nxt) {
            uint32_t n = ringbuf_write(pcb->rcvbuf, s->len, (char *) s->data);
            int waiting = pcb->ooo != NULL;

            pcb->rcv_nxt += n;
            fin_now = fin && n == s->len;

            if (!fin_now && waiting)
                fin_now = ooo_drain(pcb);

            /* a hole filled, or the FIN: acked now (fin_input) */
            if (n && !fin_now) {
                if (waiting)
                    tcp_send_ack(pcb);
                else
                    tcp_dack(pcb);
            }

            if (n)
                tcp_wakeup(pcb);
        } else {
            /* ahead: waits; the duplicate ACK says what's missing */
            ooo_insert(pcb, nb, s, fin);
            nb = NULL;
            tcp_send_ack(pcb);
        }
    } else if (fin && s->seq == pcb->rcv_nxt) {
        fin_now = 1;
    }

    if (fin_now)
        fin_input(pcb);

    /* data acked, the window moved: more to send */
    if (pcb->state != TCP_CLOSED)
        tcp_output(pcb);

done:
    if (nb)
        netbuf_free(nb);
}

void tcp_input(struct netif *netif, struct netbuf *nb, const struct ipv4_hdr *ih)
{
    struct tcp_hdr *th = (struct tcp_hdr *) nb->data;
    size_t hlen;

    if (nb->len < sizeof(*th) || (hlen = (th->off >> 4) * 4) < sizeof(*th) || hlen > nb->len ||
        inet_checksum_pseudo(ih->src, ih->dst, IP_PROTO_TCP, th, nb->len)) {
        netif->stats.rx_errors++;
        netbuf_free(nb);
        return;
    }

    /* to one of our addresses, not a broadcast */
    if (!ipv4_is_local(ih->dst)) {
        netbuf_free(nb);
        return;
    }

    struct seg s = {
        .seq   = ntohl(th->seq),
        .ack   = ntohl(th->ack),
        .win   = ntohs(th->win),
        .flags = th->flags,
        .data  = nb->data + hlen,
        .len   = nb->len - hlen,
    };

    options(th, hlen, &s);

    struct tcp_pcb *pcb = tcp_lookup(ih->dst, th->dst, ih->src, th->src);

    if (!pcb) {
        struct tcp_pcb *l = tcp_listener(ih->dst, th->dst);

        if (l)
            listen_input(l, ih, th, &s, netif);
        else
            tcp_reply_rst(netif, ih, th, s.len);

        netbuf_free(nb);
        return;
    }

    if (pcb->state == TCP_SYN_SENT) {
        syn_sent_input(pcb, ih, th, &s, netif);
        netbuf_free(nb);
        return;
    }

    segment_input(pcb, nb, &s, ih, th, netif);
}
