/*
 * The resolver: host names to IPv4 addresses and back. /etc/hosts first,
 * then the DNS (RFC 1035): the servers /etc/resolv.conf names (127.0.0.1
 * if none), each asked in turn over UDP, twice; over TCP if the answer is
 * truncated. A name without a dot is tried in the search domains first
 * (search, domain), as glibc does with ndots:1.
 */

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "resolver.h"

#define DNS_PORT        53
#define MAX_SERVERS     3
#define MAX_SEARCH      6
#define TIMEOUT_MS      2000
#define ATTEMPTS        2
#define MAX_PACKET      65535

#define T_A             1
#define T_CNAME         5
#define T_PTR           12
#define C_IN            1

struct conf {
    struct in_addr servers[MAX_SERVERS];
    int nservers;
    char search[MAX_SEARCH][256];
    int nsearch;
};

static void read_conf(struct conf *c)
{
    memset(c, 0, sizeof(*c));

    FILE *f = fopen("/etc/resolv.conf", "r");
    char line[512];

    while (f && fgets(line, sizeof(line), f)) {
        char *key = strtok(line, " \t\r\n");

        if (!key || *key == '#' || *key == ';')
            continue;

        if (!strcmp(key, "nameserver")) {
            char *v = strtok(NULL, " \t\r\n");

            if (v && c->nservers < MAX_SERVERS && inet_aton(v, &c->servers[c->nservers]))
                c->nservers++;
        } else if (!strcmp(key, "search") || !strcmp(key, "domain")) {
            /* the last one says */
            c->nsearch = 0;

            for (char *v; (v = strtok(NULL, " \t\r\n")) && c->nsearch < MAX_SEARCH; )
                snprintf(c->search[c->nsearch++], sizeof(c->search[0]), "%s", v);
        }
    }

    if (f)
        fclose(f);

    if (!c->nservers) {
        c->servers[0].s_addr = htonl(INADDR_LOOPBACK);
        c->nservers = 1;
    }
}

/*
 * /etc/hosts: "address name aliases...", # comments
 */

int __hosts_lookup(const char *name, struct in_addr *addrs, int max, char *canon, size_t canonlen)
{
    FILE *f = fopen("/etc/hosts", "r");
    char line[512];
    int n = 0;

    while (f && n < max && fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');

        if (hash)
            *hash = '\0';

        char *addr = strtok(line, " \t\r\n");
        struct in_addr a;

        if (!addr || !inet_aton(addr, &a))
            continue;

        char *first = strtok(NULL, " \t\r\n");

        for (char *h = first; h; h = strtok(NULL, " \t\r\n")) {
            if (!strcasecmp(h, name)) {
                if (!n && canon)
                    snprintf(canon, canonlen, "%s", first);
                addrs[n++] = a;
                break;
            }
        }
    }

    if (f)
        fclose(f);

    return n;
}

int __hosts_reverse(struct in_addr addr, char *name, size_t len)
{
    FILE *f = fopen("/etc/hosts", "r");
    char line[512];
    int found = 0;

    while (f && !found && fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');

        if (hash)
            *hash = '\0';

        char *a = strtok(line, " \t\r\n");
        char *h = strtok(NULL, " \t\r\n");
        struct in_addr in;

        if (a && h && inet_aton(a, &in) && in.s_addr == addr.s_addr) {
            snprintf(name, len, "%s", h);
            found = 1;
        }
    }

    if (f)
        fclose(f);

    return found;
}

/*
 * The DNS's messages
 */

/* "a.b.c" as labels; its length, -1 if it isn't a name */
static int name_encode(const char *name, unsigned char *p, size_t room)
{
    size_t n = 0;

    while (*name) {
        const char *dot = strchr(name, '.');
        size_t len = dot ? (size_t) (dot - name) : strlen(name);

        if (!len || len > 63 || n + len + 2 > room || n + len + 2 > 255)
            return -1;

        p[n++] = len;
        memcpy(p + n, name, len);
        n += len;
        name += len;

        if (*name == '.')
            ++name;
    }

    p[n++] = 0;
    return n;
}

/* the name at `off' (following pointers) into out; the offset past it
 * where it is, -1 if it is broken */
static int name_decode(const unsigned char *msg, size_t len, size_t off, char *out, size_t outlen)
{
    size_t o = 0, end = 0;
    int jumps = 0;

    if (outlen)
        out[0] = '\0';

    for (;;) {
        if (off >= len)
            return -1;

        unsigned c = msg[off];

        if (!c) {
            if (!end)
                end = off + 1;
            break;
        }

        if ((c & 0xc0) == 0xc0) {
            if (off + 1 >= len || ++jumps > 32)
                return -1;
            if (!end)
                end = off + 2;
            off = ((c & 0x3f) << 8) | msg[off + 1];
            continue;
        }

        if (c > 63 || off + 1 + c > len)
            return -1;

        if (o && o + 1 < outlen)
            out[o++] = '.';

        for (unsigned i = 0; i < c && o + 1 < outlen; ++i)
            out[o++] = msg[off + 1 + i];

        if (outlen)
            out[o] = '\0';

        off += 1 + c;
    }

    return end;
}

static int query_build(unsigned char *q, size_t room, uint16_t id, const char *name, int type)
{
    if (room < 12 + 4)
        return -1;

    memset(q, 0, 12);
    q[0] = id >> 8;
    q[1] = id & 0xff;
    q[2] = 0x01;            /* recursion desired */
    q[5] = 1;               /* one question */

    int n = name_encode(name, q + 12, room - 12 - 4);

    if (n < 0)
        return -1;

    unsigned char *t = q + 12 + n;
    t[0] = 0;
    t[1] = type;
    t[2] = 0;
    t[3] = C_IN;

    return 12 + n + 4;
}

static int tcp_exchange(struct in_addr server, const unsigned char *q, int qlen, unsigned char *ans, int size)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in sin = {0};
    struct timeval tv = {TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000};

    if (fd < 0)
        return -1;

    sin.sin_family = AF_INET;
    sin.sin_port = htons(DNS_PORT);
    sin.sin_addr = server;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    unsigned char hdr[2] = {qlen >> 8, qlen & 0xff};
    int len = -1;

    if (!connect(fd, (struct sockaddr *) &sin, sizeof(sin)) &&
        send(fd, hdr, 2, 0) == 2 && send(fd, q, qlen, 0) == qlen &&
        recv(fd, hdr, 2, MSG_WAITALL) == 2) {
        int want = hdr[0] << 8 | hdr[1];

        if (want <= size && recv(fd, ans, want, MSG_WAITALL) == want)
            len = want;
    }

    close(fd);
    return len;
}

/* the answer's length (an answer of `id' from one of the servers); 0 if
 * none answered */
static int exchange(const struct conf *c, const unsigned char *q, int qlen, unsigned char *ans, int size)
{
    uint16_t id = q[0] << 8 | q[1];
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (fd < 0)
        return 0;

    for (int attempt = 0; attempt < ATTEMPTS; ++attempt) {
        for (int i = 0; i < c->nservers; ++i) {
            struct sockaddr_in sin = {0};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(DNS_PORT);
            sin.sin_addr = c->servers[i];

            if (sendto(fd, q, qlen, 0, (struct sockaddr *) &sin, sizeof(sin)) != qlen)
                continue;

            struct pollfd pfd = {fd, POLLIN, 0};

            while (poll(&pfd, 1, TIMEOUT_MS) == 1) {
                struct sockaddr_in from;
                socklen_t fromlen = sizeof(from);
                int n = recvfrom(fd, ans, size, 0, (struct sockaddr *) &from, &fromlen);

                /* refused (nobody there): the next one */
                if (n < 0)
                    break;

                /* someone else's, or another question's */
                if (n < 12 || from.sin_addr.s_addr != sin.sin_addr.s_addr ||
                    (ans[0] << 8 | ans[1]) != id || !(ans[2] & 0x80))
                    continue;

                close(fd);

                /* truncated: over TCP */
                if (ans[2] & 0x02) {
                    int t = tcp_exchange(c->servers[i], q, qlen, ans, size);
                    return t > 0 ? t : n;
                }

                return n;
            }
        }
    }

    close(fd);
    return 0;
}

/*
 * One name, one type: the addresses (A, through CNAMEs, the last one's
 * name is canon) or the name (PTR). 0 or an EAI_* error.
 */
static int dns_ask(const struct conf *c, const char *name, int type, struct in_addr *addrs, int max,
                   int *naddrs, char *canon, size_t canonlen)
{
    unsigned char q[512], *ans = malloc(MAX_PACKET);
    uint16_t id = arc4random() & 0xffff;
    int qlen = query_build(q, sizeof(q), id, name, type);

    if (!ans)
        return EAI_MEMORY;

    if (qlen < 0) {
        free(ans);
        return EAI_NONAME;
    }

    int len = exchange(c, q, qlen, ans, MAX_PACKET);
    int err = 0;

    *naddrs = 0;

    if (!len) {
        err = EAI_AGAIN;
        goto out;
    }

    switch (ans[3] & 0x0f) {
        case 0: break;
        case 3: err = EAI_NONAME; goto out;     /* no such name */
        case 2: err = EAI_AGAIN; goto out;      /* the server failed */
        default: err = EAI_FAIL; goto out;
    }

    int qd = ans[4] << 8 | ans[5], an = ans[6] << 8 | ans[7];
    int off = 12;
    char tmp[256];

    for (int i = 0; i < qd && off >= 0; ++i) {
        off = name_decode(ans, len, off, tmp, sizeof(tmp));
        if (off >= 0)
            off += 4;
    }

    if (canon && canonlen)
        snprintf(canon, canonlen, "%s", name);

    for (int i = 0; i < an && off >= 0 && off + 10 <= len; ++i) {
        off = name_decode(ans, len, off, tmp, sizeof(tmp));

        if (off < 0 || off + 10 > len)
            break;

        int rtype = ans[off] << 8 | ans[off + 1];
        int rclass = ans[off + 2] << 8 | ans[off + 3];
        int rdlen = ans[off + 8] << 8 | ans[off + 9];
        int rdata = off + 10;

        if (rdata + rdlen > len)
            break;

        if (rclass == C_IN) {
            if (rtype == T_A && type == T_A && rdlen == 4 && *naddrs < max)
                memcpy(&addrs[(*naddrs)++], ans + rdata, 4);
            else if ((rtype == T_CNAME || (rtype == T_PTR && type == T_PTR)) && canon)
                name_decode(ans, len, rdata, canon, canonlen);

            if (rtype == T_PTR && type == T_PTR)
                *naddrs = 1;
        }

        off = rdata + rdlen;
    }

    if (!*naddrs)
        err = EAI_NONAME;

out:
    free(ans);
    return err;
}

/* `name's IPv4 addresses (max at most), its canonical name: 0, or EAI_* */
int __resolve(const char *name, struct in_addr *addrs, int max, int *naddrs, char *canon, size_t canonlen)
{
    if ((*naddrs = __hosts_lookup(name, addrs, max, canon, canonlen)))
        return 0;

    /* without the hosts file: localhost is ours */
    if (!strcasecmp(name, "localhost") || !strcasecmp(name, "localhost.")) {
        addrs[0].s_addr = htonl(INADDR_LOOPBACK);
        *naddrs = 1;
        if (canon)
            snprintf(canon, canonlen, "localhost");
        return 0;
    }

    struct conf c;
    read_conf(&c);

    size_t len = strlen(name);
    char fqdn[512];
    int err = EAI_NONAME;

    if (!len || len > 253)
        return EAI_NONAME;

    /* "name." is that, nothing else */
    if (name[len - 1] == '.') {
        snprintf(fqdn, sizeof(fqdn), "%.*s", (int) len - 1, name);
        return dns_ask(&c, fqdn, T_A, addrs, max, naddrs, canon, canonlen);
    }

    /* no dot: the search domains first */
    if (!strchr(name, '.')) {
        for (int i = 0; i < c.nsearch; ++i) {
            snprintf(fqdn, sizeof(fqdn), "%s.%s", name, c.search[i]);
            err = dns_ask(&c, fqdn, T_A, addrs, max, naddrs, canon, canonlen);

            if (!err || err == EAI_AGAIN)
                return err;
        }
    }

    int e = dns_ask(&c, name, T_A, addrs, max, naddrs, canon, canonlen);

    /* no answer at all is worth trying again, more than "no such name" */
    return e == EAI_NONAME && err == EAI_AGAIN ? err : e;
}

/* the name of `addr': /etc/hosts, then a PTR record (d.c.b.a.in-addr.arpa) */
int __resolve_reverse(struct in_addr addr, char *name, size_t len)
{
    if (__hosts_reverse(addr, name, len))
        return 0;

    struct conf c;
    read_conf(&c);

    const unsigned char *b = (const unsigned char *) &addr;
    char q[64];
    struct in_addr unused;
    int n;

    snprintf(q, sizeof(q), "%u.%u.%u.%u.in-addr.arpa", b[3], b[2], b[1], b[0]);
    return dns_ask(&c, q, T_PTR, &unused, 1, &n, name, len);
}
