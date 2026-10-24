#ifndef _NET_TCP_H
#define _NET_TCP_H

#include <core/system.h>
#include <ds/hashmap.h>
#include <ds/queue.h>
#include <ds/ringbuf.h>
#include <net/inet.h>
#include <sys/callout.h>

/*
 * TCP (RFC 9293), in net/inet/tcp_*.c. See docs/kernel/networking.md.
 */

/** A TCP segment's header. */
struct tcp_hdr {
    uint16_t src, dst;
    uint32_t seq, ack;
    uint8_t  off;           /* the header's length in 32-bit words, shifted left by 4 */
    uint8_t  flags;
    uint16_t win;
    uint16_t checksum;
    uint16_t urg;
} __packed;

/* A segment's flags. */
#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PSH  0x08
#define TH_ACK  0x10
#define TH_URG  0x20

/* A connection's states (RFC 9293 3.3.2). */
enum tcp_state {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
};

/* Compares sequence numbers, which wrap around (modulo 2^32). */
#define SEQ_LT(a, b)    ((int32_t) ((a) - (b)) < 0)
#define SEQ_LEQ(a, b)   ((int32_t) ((a) - (b)) <= 0)
#define SEQ_GT(a, b)    ((int32_t) ((a) - (b)) > 0)
#define SEQ_GEQ(a, b)   ((int32_t) ((a) - (b)) >= 0)

/* The buffers' size, each way: 65535 bytes, the most a window can say. */
#define TCP_BUFSIZE     65536

/* The peer's maximum segment size, if it doesn't say (RFC 9293). */
#define TCP_MSS_DEFAULT 536

/**
 * A connection (its control block, or PCB). It may outlive its socket,
 * to send what is left, or in TIME_WAIT, and it has no socket until it
 * is accepted.
 */
struct tcp_pcb {
    int state;

    /* in the network's byte order */
    uint32_t laddr, raddr;
    uint16_t lport, rport;

    struct tcp_pcb *next;           /* the next PCB of all of them (for ports, listeners) */
    struct hashmap_node *hnode;     /* its node in the hash, by addresses and ports, once it has them */

    /* sending: snd_una is the first byte of sndbuf, and snd_max the end
     * of what was sent (snd_nxt goes back from there to retransmit) */
    uint32_t iss, snd_una, snd_nxt, snd_max;
    uint32_t snd_wnd, snd_wl1, snd_wl2;
    uint16_t mss;                   /* the most data a segment carries */
    struct ringbuf *sndbuf;
    int fin_queued;                 /* close() or shutdown() came: send a FIN after the data */

    /* receiving: rcvbuf holds the data before rcv_nxt; ooo holds the
     * segments that came out of order */
    uint32_t irs, rcv_nxt, rcv_adv;
    struct ringbuf *rcvbuf;
    struct netbuf *ooo;
    int fin_received;               /* the peer's FIN came: the end is after rcvbuf */

    /* the retransmission timeout (RFC 6298), in ns */
    uint64_t srtt, rttvar, rto;
    int rtt_timing;                 /* a segment is being timed (one not retransmitted) */
    uint32_t rtt_seq;
    uint64_t rtt_start;
    int retries;
    int persist;                    /* the peer's zero window is being probed (rexmt) */

    /* congestion control (RFC 5681, 6582) */
    uint32_t cwnd, ssthresh;
    int dupacks;
    int in_recovery;
    uint32_t recover;

    int ack_pending;                /* how many segments aren't acknowledged yet (delayed ACK) */
    int nodelay;                    /* TCP_NODELAY: no Nagle algorithm */
    int keepalive;                  /* SO_KEEPALIVE */
    int keep_probes;
    int reuseaddr;

    struct callout rexmt;           /* the retransmission, or persist, timer */
    struct callout dack;            /* the delayed ACK */
    struct callout timer2;          /* TIME_WAIT, FIN_WAIT_2, keepalive */

    /* for a listener: the handshakes going on, and the connections done
     * (for accept()); for a connection, its listener until it is
     * accepted */
    struct tcp_pcb *parent;
    struct tcp_pcb *accept_head, *accept_tail, *accept_next;
    int backlog, qlen;

    int detached;                   /* its socket was closed: it goes once it's done */
    int connected;                  /* whether it ever was (ENOTCONN, EPIPE) */
    int error;                      /* an error to report */
    int shut_rd;
    uint64_t rcvtimeo, sndtimeo;    /* in ns; 0 means none */
    struct waitq wait;              /* connect, accept, send and recv sleep here */
};

/* tcp_input.c: a segment that came */
void tcp_input(struct netif *netif, struct netbuf *nb, const struct ipv4_hdr *ih);

/* tcp_output.c: sending segments, and the timers */
void tcp_send_keepalive(struct tcp_pcb *pcb);
void tcp_output(struct tcp_pcb *pcb);
void tcp_send_ack(struct tcp_pcb *pcb);
void tcp_send_syn(struct tcp_pcb *pcb);
void tcp_send_rst(struct tcp_pcb *pcb);
void tcp_reply_rst(struct netif *netif, const struct ipv4_hdr *ih, const struct tcp_hdr *th, size_t len);
void tcp_rexmt_start(struct tcp_pcb *pcb);
void tcp_rtt_update(struct tcp_pcb *pcb, uint64_t rtt);
uint16_t tcp_rcv_window(struct tcp_pcb *pcb);
void tcp_dack(struct tcp_pcb *pcb);

/* tcp_socket.c: the connections, and their sockets */
extern struct tcp_pcb *tcp_pcbs;
struct tcp_pcb *tcp_pcb_new(void);
void tcp_pcb_free(struct tcp_pcb *pcb);
struct tcp_pcb *tcp_lookup(uint32_t laddr, uint16_t lport, uint32_t raddr, uint16_t rport);
struct tcp_pcb *tcp_listener(uint32_t laddr, uint16_t lport);
int  tcp_insert(struct tcp_pcb *pcb);
uint32_t tcp_iss(struct tcp_pcb *pcb);
void tcp_fin_wait_2(struct tcp_pcb *pcb);
void tcp_keepalive_reset(struct tcp_pcb *pcb);
void tcp_established(struct tcp_pcb *pcb);
void tcp_drop(struct tcp_pcb *pcb, int err);
void tcp_closed(struct tcp_pcb *pcb);
void tcp_time_wait(struct tcp_pcb *pcb);
void tcp_wakeup(struct tcp_pcb *pcb);

#endif /* ! _NET_TCP_H */
