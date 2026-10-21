/*
 * TCP against another one: QEMU's user network, which has its own (from
 * BSD's), between the guest and tests/tcp/host.py on the host
 *
 * - the guest connects to 10.0.2.100:7001 (QEMU's guestfwd, to the host's
 *   listener), sends 2 MiB and reads the host's verdict, 2 bytes
 * - then it serves on port 7000 (hostfwd): an echo server, a child for
 *   each connection, until the host has made all its connections (6)
 */

#include "../lib/aqtest.h"

#include <aquila/in.h>
#include <aquila/socket.h>

#define OUT_BYTES   (2 * 1024 * 1024)
#define CONNS       6

#define MIN(a, b)   ((a) < (b) ? (a) : (b))

static uint16_t htons(uint16_t v) { return (uint16_t) (v << 8 | v >> 8); }

static uint32_t ip4(int a, int b, int c, int d)
{
    return (uint32_t) a | (uint32_t) b << 8 | (uint32_t) c << 16 | (uint32_t) d << 24;
}

static int tcp(void)
{
    return SC(SYS_socket, AF_INET, SOCK_STREAM, 0);
}

static int sock_send(int fd, const void *buf, int len)
{
    struct aquila_socket_io_args a = {fd, (void *) buf, len, 0, 0, 0};
    return SC(SYS_send, &a, 0, 0);
}

static int sock_recv(int fd, void *buf, int len)
{
    struct aquila_socket_io_args a = {fd, buf, len, 0, 0, 0};
    return SC(SYS_recv, &a, 0, 0);
}

static unsigned char pattern(unsigned i)
{
    return (unsigned char) (i * 13 + (i >> 11));
}

static unsigned char buf[16384];

/* a connection's child: what comes goes back, until the end */
static int echo_fd;

static void echo(void)
{
    int n;

    while ((n = sock_recv(echo_fd, buf, sizeof(buf))) > 0)
        if (sock_send(echo_fd, buf, n) != n)
            SC(SYS_exit, 1, 0, 0);

    SC(SYS_exit, n == 0 ? 0 : 2, 0, 0);
}

void aq_main(void)
{
    aq_begin("tcp");

    int l = tcp();
    struct sockaddr_in me = {AF_INET, htons(7000), {0}, {0}};
    check("listen", SC(SYS_bind, l, &me, sizeof(me)) || SC(SYS_listen, l, 8, 0), 0);

    /* out: 2 MiB to the host */
    int c = tcp();
    struct sockaddr_in host = {AF_INET, htons(7001), {ip4(10, 0, 2, 100)}, {0}};
    int err = -1;

    for (int i = 0; i < 50 && err; ++i) {
        if ((err = SC(SYS_connect, c, &host, sizeof(host)))) {
            SC(SYS_close, c, 0, 0);
            c = tcp();
            struct aquila_timespec ts = {0, 100000000};
            SC(SYS_nanosleep, &ts, 0, 0);
        }
    }

    check("connect to the host", err, 0);

    int sent = 0;

    while (sent < OUT_BYTES) {
        int n = MIN(OUT_BYTES - sent, (int) sizeof(buf));

        for (int i = 0; i < n; ++i)
            buf[i] = pattern(sent + i);

        int r = sock_send(c, buf, n);

        if (r <= 0)
            break;

        sent += r;
    }

    check("2 MiB to the host", sent, OUT_BYTES);
    SC(SYS_shutdown, c, SHUT_WR, 0);

    char verdict[16] = {0};
    int got = 0, n;

    while (got < 2 && (n = sock_recv(c, verdict + got, 2 - got)) > 0)
        got += n;

    check("the host got them right", got == 2 && verdict[0] == 'o' && verdict[1] == 'k', 1);
    SC(SYS_close, c, 0, 0);

    /* in: an echo server */
    int children = 0, fails = 0;

    for (int i = 0; i < CONNS; ++i) {
        int s = SC(SYS_accept, l, 0, 0);

        if (s < 0) {
            check("accept", s, 0);
            break;
        }

        int pid = SC(SYS_fork, 0, 0, 0);

        if (!pid) {
            echo_fd = s;
            echo();
        }

        SC(SYS_close, s, 0, 0);
        children++;
    }

    for (int i = 0; i < children; ++i) {
        int st = -1;
        SC(SYS_waitpid, -1, &st, 0);
        fails += st != 0;
    }

    check("echoed every connection", children == CONNS && !fails, 1);
    aq_end();
}
