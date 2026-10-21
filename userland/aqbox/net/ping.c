/*
 * ping: echo requests to a host, and how long their replies take, over a
 * ping socket (SOCK_DGRAM, IPPROTO_ICMP: no privileges needed)
 *
 *   ping [-c count] [-i interval] [-W timeout] <host>
 */

#include <aqbox.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip_icmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DATA_LEN 56

static volatile int interrupted;

static void on_sigint(int sig)
{
    (void) sig;
    interrupted = 1;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int resolve(const char *host, struct in_addr *addr)
{
    if (inet_aton(host, addr))
        return 0;

    struct addrinfo hints = {.ai_family = AF_INET}, *res;

    if (getaddrinfo(host, NULL, &hints, &res))
        return -1;

    *addr = ((struct sockaddr_in *) res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

AQBOX_APPLET(ping)(int argc, char **argv)
{
    int count = -1, opt;
    double interval = 1.0, timeout = 2.0;

    while ((opt = getopt(argc, argv, "c:i:W:")) != -1) {
        switch (opt) {
            case 'c': count = atoi(optarg); break;
            case 'i': interval = atof(optarg); break;
            case 'W': timeout = atof(optarg); break;
            default: goto usage;
        }
    }

    if (optind != argc - 1)
        goto usage;

    struct sockaddr_in to = {.sin_family = AF_INET};

    if (resolve(argv[optind], &to.sin_addr)) {
        fprintf(stderr, "ping: %s: unknown host\n", argv[optind]);
        return 2;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);

    if (fd < 0) {
        perror("ping: socket");
        return 2;
    }

    struct timeval tv = {(time_t) timeout, (long) ((timeout - (time_t) timeout) * 1e6)};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    signal(SIGINT, on_sigint);

    printf("PING %s (%s): %d data bytes\n", argv[optind], inet_ntoa(to.sin_addr), DATA_LEN);

    int sent = 0, received = 0;
    double min = 0, max = 0, sum = 0;

    for (int seq = 1; !interrupted && (count < 0 || seq <= count); ++seq) {
        unsigned char packet[sizeof(struct icmphdr) + DATA_LEN];
        struct icmphdr *icmp = (struct icmphdr *) packet;
        double start = now_ms();

        memset(packet, 0, sizeof(packet));
        icmp->type = ICMP_ECHO;
        icmp->un.echo.sequence = htons(seq);
        memcpy(packet + sizeof(*icmp), &start, sizeof(start));
        for (int i = sizeof(start); i < DATA_LEN; ++i)
            packet[sizeof(*icmp) + i] = i;

        if (sendto(fd, packet, sizeof(packet), 0, (struct sockaddr *) &to, sizeof(to)) < 0) {
            perror("ping: sendto");
        } else {
            ++sent;
        }

        /* the reply to this one (others, late, are skipped) */
        for (;;) {
            unsigned char reply[sizeof(packet) + 64];
            struct sockaddr_in from;
            socklen_t len = sizeof(from);
            ssize_t n = recvfrom(fd, reply, sizeof(reply), 0, (struct sockaddr *) &from, &len);

            if (n < 0) {
                if (!interrupted)
                    printf("no reply for icmp_seq=%d\n", seq);
                break;
            }

            struct icmphdr *r = (struct icmphdr *) reply;

            if (n < (ssize_t) sizeof(*r) || r->type != ICMP_ECHOREPLY ||
                ntohs(r->un.echo.sequence) != seq)
                continue;

            double ms = now_ms() - start;
            sum += ms;
            min = received == 0 || ms < min ? ms : min;
            max = ms > max ? ms : max;
            ++received;

            printf("%zd bytes from %s: icmp_seq=%d time=%.3f ms\n", n,
                   inet_ntoa(from.sin_addr), seq, ms);
            break;
        }

        if (!interrupted && (count < 0 || seq < count)) {
            double left = interval * 1000 - (now_ms() - start);
            if (left > 0)
                usleep((useconds_t) (left * 1000));
        }
    }

    printf("--- %s ping statistics ---\n", argv[optind]);
    printf("%d packets transmitted, %d received, %d%% packet loss\n", sent, received,
           sent ? (sent - received) * 100 / sent : 0);

    if (received)
        printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n", min, sum / received, max);

    return received ? 0 : 1;

usage:
    fprintf(stderr, "usage: ping [-c count] [-i interval] [-W timeout] <host>\n");
    return 2;
}
