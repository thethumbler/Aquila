/*
 * Internet addresses and their text: IPv4 (a.b.c.d, and the shorter forms
 * of inet_aton()), IPv6 isn't converted.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

int inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned long parts[4];
    int n = 0;
    const char *s = cp;

    for (;;) {
        char *end;

        if (*s < '0' || *s > '9')
            return 0;

        /* decimal, octal (0) or hexadecimal (0x), as inet_aton() takes */
        parts[n++] = strtoul(s, &end, 0);
        s = end;

        if (*s == '\0')
            break;
        if (*s != '.' || n == 4)
            return 0;
        s++;
    }

    /* a.b.c.d, a.b.c (c is 16 bits), a.b (b is 24 bits), a (32 bits) */
    static const unsigned long max[4] = {0xffffffff, 0xffffff, 0xffff, 0xff};
    in_addr_t addr = 0;

    for (int i = 0; i < n - 1; ++i) {
        if (parts[i] > 0xff)
            return 0;
        addr |= parts[i] << (24 - 8 * i);
    }

    if (parts[n - 1] > max[n - 1])
        return 0;
    addr |= parts[n - 1];

    if (inp)
        inp->s_addr = htonl(addr);
    return 1;
}

in_addr_t inet_addr(const char *cp)
{
    struct in_addr in;
    return inet_aton(cp, &in) ? in.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in, buf, sizeof(buf));
    return buf;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    /* only the four parts, in decimal */
    unsigned char b[4];
    const char *s = src;

    for (int i = 0; i < 4; ++i) {
        int v = 0, digits = 0;

        while (*s >= '0' && *s <= '9' && digits < 4) {
            v = v * 10 + (*s++ - '0');
            digits++;
        }

        if (!digits || v > 255 || (digits > 1 && s[-digits] == '0'))
            return 0;

        b[i] = v;

        if (i < 3 && *s++ != '.')
            return 0;
    }

    if (*s)
        return 0;

    memcpy(dst, b, 4);
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }

    const unsigned char *b = src;
    char buf[INET_ADDRSTRLEN];
    int len = snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);

    if ((socklen_t) len >= size) {
        errno = ENOSPC;
        return NULL;
    }

    memcpy(dst, buf, len + 1);
    return dst;
}
