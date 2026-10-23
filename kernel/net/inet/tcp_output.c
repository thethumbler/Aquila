/** \file
 * TCP's output: segments from the send buffer, as the window (the
 * peer's, and the congestion window) lets them go; retransmission (RFC
 * 6298) and the zero window's probes, from the same timer; ACKs, delayed
 * (RFC 1122: every second segment, or after DACK_DELAY)
 */

#include <core/system.h>
#include <core/arch.h>
#include <core/string.h>
#include <net/byteorder.h>
#include <net/inet.h>
#include <net/netif.h>
#include <net/tcp.h>

#include <aquila/errno.h>

#define RTO_MIN         1000000000ULL       /* RFC 6298: 1 s */
#define RTO_MAX         60000000000ULL
#define RTT_GRANULARITY 2000000ULL          /* the clock's: a tick */
#define DACK_DELAY      100000000ULL        /* 100 ms */

#define SYN_RETRIES     5                   /* 1 + 2 + 4 + 8 + 16 + 32 s: 63 s */
#define DATA_RETRIES    8                   /* about 2 minutes, RTO_MAX past 32 s */

static void rexmt_timeout(void *arg);

/** the most data a segment of ours has: the interface's MTU, less the
 * headers */
static uint16_t our_mss(struct tcp_pcb *pcb)
{
    struct netif *netif = ipv4_route(pcb->raddr);
    return netif ? netif->mtu - sizeof(struct ipv4_hdr) - sizeof(struct tcp_hdr) : TCP_MSS_DEFAULT;
}

/**
 * The window advertised: the room in rcvbuf. Its right edge doesn't move
 * by less than a segment (or half the buffer): no small windows (RFC 9293
 * 3.8.6.2.2, the receiver's silly window avoidance), and it never moves
 * back.
 */
uint16_t tcp_rcv_window(struct tcp_pcb *pcb)
{
    uint32_t room = TCP_BUFSIZE - 1 - ringbuf_available(pcb->rcvbuf);
    uint32_t cur = SEQ_GT(pcb->rcv_adv, pcb->rcv_nxt) ? pcb->rcv_adv - pcb->rcv_nxt : 0;
    uint32_t step = MIN(TCP_BUFSIZE / 2, pcb->mss);

    if (room < cur || (room - cur) < step)
        room = cur;

    return room > 0xffff ? 0xffff : room;
}

/** a segment of `len' bytes of sndbuf from `off', at `seq'; the ACK is
 * rcv_nxt's if TH_ACK */
static int segment(struct tcp_pcb *pcb, uint32_t seq, int flags, size_t off, size_t len)
{
    struct netif *netif = ipv4_route(pcb->raddr);

    if (!netif)
        return -ENETUNREACH;

    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return -ENOBUFS;

    if (len)
        ringbuf_read_noconsume(pcb->sndbuf, off, len, netbuf_put(nb, len));

    /* a SYN says the MSS */
    size_t hlen = sizeof(struct tcp_hdr) + (flags & TH_SYN ? 4 : 0);
    struct tcp_hdr *th = netbuf_push(nb, hlen);
    uint16_t win = tcp_rcv_window(pcb);

    th->src      = pcb->lport;
    th->dst      = pcb->rport;
    th->seq      = htonl(seq);
    th->ack      = flags & TH_ACK ? htonl(pcb->rcv_nxt) : 0;
    th->off      = (hlen / 4) << 4;
    th->flags    = flags;
    th->win      = htons(win);
    th->checksum = 0;
    th->urg      = 0;

    if (flags & TH_SYN) {
        uint8_t *opt = (uint8_t *) (th + 1);
        uint16_t mss = our_mss(pcb);

        opt[0] = 2;     /* MSS */
        opt[1] = 4;
        opt[2] = mss >> 8;
        opt[3] = mss & 0xff;
    }

    th->checksum = inet_checksum_pseudo(pcb->laddr, pcb->raddr, IP_PROTO_TCP, th, nb->len);

    /* what it acks is acked */
    if (flags & TH_ACK) {
        pcb->rcv_adv = pcb->rcv_nxt + win;
        pcb->ack_pending = 0;
        callout_stop(&pcb->dack);
    }

    return ipv4_output(netif, pcb->laddr, pcb->raddr, IP_PROTO_TCP, nb);
}

void tcp_rexmt_start(struct tcp_pcb *pcb)
{
    if (!pcb->rexmt.pending)
        net_callout_set(&pcb->rexmt, pcb->rto, rexmt_timeout, pcb);
}

/** SYN, or SYN and ACK (SYN_RECEIVED), its retransmission too */
void tcp_send_syn(struct tcp_pcb *pcb)
{
    int flags = TH_SYN | (pcb->state == TCP_SYN_RECEIVED ? TH_ACK : 0);

    /* the first one is timed */
    if (!pcb->retries) {
        pcb->rtt_timing = 1;
        pcb->rtt_seq = pcb->iss;
        pcb->rtt_start = arch_rtime_ns();
    }

    segment(pcb, pcb->iss, flags, 0, 0);

    pcb->snd_nxt = pcb->snd_max = pcb->iss + 1;
    tcp_rexmt_start(pcb);
}

void tcp_send_ack(struct tcp_pcb *pcb)
{
    segment(pcb, pcb->snd_nxt, TH_ACK, 0, 0);
}

/** a keepalive probe: a byte already acked (none of it), for an ACK */
void tcp_send_keepalive(struct tcp_pcb *pcb)
{
    segment(pcb, pcb->snd_una - 1, TH_ACK, 0, 0);
}

void tcp_send_rst(struct tcp_pcb *pcb)
{
    segment(pcb, pcb->snd_nxt, TH_RST | TH_ACK, 0, 0);
}

/** a segment for no connection: RST (RFC 9293 3.10.7.1) */
void tcp_reply_rst(struct netif *netif, const struct ipv4_hdr *ih, const struct tcp_hdr *th, size_t len)
{
    if (th->flags & TH_RST)
        return;

    struct netbuf *nb = netbuf_alloc();

    if (!nb)
        return;

    struct tcp_hdr *rh = netbuf_push(nb, sizeof(*rh));

    memset(rh, 0, sizeof(*rh));
    rh->src = th->dst;
    rh->dst = th->src;
    rh->off = (sizeof(*rh) / 4) << 4;

    if (th->flags & TH_ACK) {
        rh->seq   = th->ack;
        rh->flags = TH_RST;
    } else {
        uint32_t seglen = len + !!(th->flags & TH_SYN) + !!(th->flags & TH_FIN);

        rh->ack   = htonl(ntohl(th->seq) + seglen);
        rh->flags = TH_RST | TH_ACK;
    }

    rh->checksum = inet_checksum_pseudo(ih->dst, ih->src, IP_PROTO_TCP, rh, nb->len);
    ipv4_output(netif, ih->dst, ih->src, IP_PROTO_TCP, nb);
}

/** the states the data and FIN are sent in */
static int sending(struct tcp_pcb *pcb)
{
    switch (pcb->state) {
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
        case TCP_FIN_WAIT_1:
        case TCP_CLOSING:
        case TCP_LAST_ACK:
            return 1;
    }

    return 0;
}

/**
 * As much as the windows let go: segments of at most mss bytes; a small
 * one only if it's the last of the data and nothing is unacked (Nagle,
 * unless TCP_NODELAY), and not because the peer's window is small (the
 * sender's silly window avoidance). The FIN goes with the last of the
 * data.
 */
void tcp_output(struct tcp_pcb *pcb)
{
    if (!sending(pcb))
        return;

    for (;;) {
        uint32_t buffered = ringbuf_available(pcb->sndbuf);
        uint32_t flight   = pcb->snd_nxt - pcb->snd_una;
        uint32_t unsent   = buffered > flight ? buffered - flight : 0;
        uint32_t wnd      = MIN(pcb->snd_wnd, pcb->cwnd);
        uint32_t usable   = wnd > flight ? wnd - flight : 0;
        uint32_t len      = MIN(unsent, MIN(usable, pcb->mss));
        int fin = pcb->fin_queued && len == unsent &&
                  pcb->snd_nxt + len == pcb->snd_una + buffered;

        if (!len && !fin)
            break;

        if (len < pcb->mss && !fin) {
            if (len == unsent && flight && !pcb->nodelay)
                break;

            if (len < unsent && len < pcb->snd_wnd / 2)
                break;
        }

        uint32_t seq = pcb->snd_nxt;
        int flags = TH_ACK | (fin ? TH_FIN : 0) | (len && len == unsent ? TH_PSH : 0);

        if (segment(pcb, seq, flags, seq - pcb->snd_una, len))
            break;

        /* new data is timed, one segment at a time (Karn: not what is
         * retransmitted) */
        if (!pcb->rtt_timing && len && seq == pcb->snd_max) {
            pcb->rtt_timing = 1;
            pcb->rtt_seq = seq;
            pcb->rtt_start = arch_rtime_ns();
        }

        pcb->snd_nxt += len + fin;

        if (SEQ_GT(pcb->snd_nxt, pcb->snd_max))
            pcb->snd_max = pcb->snd_nxt;

        tcp_rexmt_start(pcb);

        if (fin)
            break;
    }

    /* a zero window, nothing out: probe it (persist) */
    uint32_t buffered = ringbuf_available(pcb->sndbuf);

    if (!pcb->snd_wnd && buffered && pcb->snd_nxt == pcb->snd_una && !pcb->rexmt.pending) {
        pcb->persist = 1;
        net_callout_set(&pcb->rexmt, pcb->rto, rexmt_timeout, pcb);
    }
}

/** RFC 6298 */
void tcp_rtt_update(struct tcp_pcb *pcb, uint64_t rtt)
{
    if (!pcb->srtt) {
        pcb->srtt = rtt;
        pcb->rttvar = rtt / 2;
    } else {
        uint64_t delta = pcb->srtt > rtt ? pcb->srtt - rtt : rtt - pcb->srtt;

        pcb->rttvar = (3 * pcb->rttvar + delta) / 4;
        pcb->srtt = (7 * pcb->srtt + rtt) / 8;
    }

    pcb->rto = pcb->srtt + MAX(RTT_GRANULARITY, 4 * pcb->rttvar);
    pcb->rto = MIN(MAX(pcb->rto, RTO_MIN), RTO_MAX);
}

static void dack_timeout(void *arg)
{
    tcp_send_ack(arg);
}

/** a segment to ack: now if it's the second, else in DACK_DELAY */
void tcp_dack(struct tcp_pcb *pcb)
{
    if (++pcb->ack_pending >= 2)
        tcp_send_ack(pcb);
    else if (!pcb->dack.pending)
        net_callout_set(&pcb->dack, DACK_DELAY, dack_timeout, pcb);
}

static void rexmt_timeout(void *arg)
{
    struct tcp_pcb *pcb = arg;

    /* the zero window: a byte past it, to see if it opened; backing off,
     * for as long as the peer answers */
    if (pcb->persist) {
        if (pcb->snd_wnd || !ringbuf_available(pcb->sndbuf)) {
            pcb->persist = 0;
            tcp_output(pcb);
            return;
        }

        pcb->snd_nxt = pcb->snd_una;
        segment(pcb, pcb->snd_una, TH_ACK, 0, 1);
        pcb->snd_nxt = pcb->snd_una + 1;

        if (SEQ_GT(pcb->snd_nxt, pcb->snd_max))
            pcb->snd_max = pcb->snd_nxt;

        pcb->rto = MIN(pcb->rto * 2, RTO_MAX);
        net_callout_set(&pcb->rexmt, pcb->rto, rexmt_timeout, pcb);
        return;
    }

    int syn = pcb->state == TCP_SYN_SENT || pcb->state == TCP_SYN_RECEIVED;

    /* everything acked */
    if (!syn && pcb->snd_una == pcb->snd_max)
        return;

    if (++pcb->retries > (syn ? SYN_RETRIES : DATA_RETRIES)) {
        tcp_drop(pcb, ETIMEDOUT);
        return;
    }

    /* backing off; a loss: from one segment again (RFC 5681) */
    uint32_t flight = pcb->snd_max - pcb->snd_una;

    pcb->rto = MIN(pcb->rto * 2, RTO_MAX);
    pcb->ssthresh = MAX(flight / 2, 2U * pcb->mss);
    pcb->cwnd = pcb->mss;
    pcb->dupacks = 0;
    pcb->in_recovery = 0;
    pcb->rtt_timing = 0;

    if (syn) {
        tcp_send_syn(pcb);
        return;
    }

    pcb->snd_nxt = pcb->snd_una;
    tcp_output(pcb);
}
