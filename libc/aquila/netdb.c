/*
 * Host and service names, and network interfaces. There is no network or
 * resolver yet: only numeric IPv4 addresses are hosts, no names are found,
 * and there are no interfaces.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>

int h_errno;

struct hostent *gethostbyname(const char *name)
{
    static struct in_addr addr;
    static char *addrs[2] = {(char *) &addr, NULL};
    static char *aliases[1] = {NULL};
    static char hname[INET_ADDRSTRLEN];
    static struct hostent host = {hname, aliases, AF_INET, 4, addrs};

    if (!name || !inet_aton(name, &addr)) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }

    inet_ntop(AF_INET, &addr, hname, sizeof(hname));
    return &host;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    (void) addr; (void) len; (void) type;
    h_errno = HOST_NOT_FOUND;
    return NULL;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    (void) name; (void) proto;
    return NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
    (void) port; (void) proto;
    return NULL;
}

struct protoent *getprotobyname(const char *name)
{
    (void) name;
    return NULL;
}

/* a number, the port, -1 if it isn't one */
static int port_number(const char *service)
{
    char *end;
    long port = strtol(service, &end, 10);

    return *service && !*end && port >= 0 && port <= 65535 ? port : -1;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int flags = hints ? hints->ai_flags : 0;
    struct in_addr addr;
    int port = 0;

    if (!node && !service)
        return EAI_NONAME;

    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;

    if (service && (port = port_number(service)) < 0)
        return EAI_SERVICE;

    if (!node)
        addr.s_addr = htonl(flags & AI_PASSIVE ? INADDR_ANY : INADDR_LOOPBACK);
    else if (!inet_aton(node, &addr))
        return EAI_NONAME;

    struct {
        struct addrinfo ai;
        struct sockaddr_in sin;
    } *r = calloc(1, sizeof(*r));

    if (!r)
        return EAI_MEMORY;

    r->sin.sin_family = AF_INET;
    r->sin.sin_port = htons(port);
    r->sin.sin_addr = addr;

    r->ai.ai_family = AF_INET;
    r->ai.ai_socktype = hints ? hints->ai_socktype : 0;
    r->ai.ai_protocol = hints ? hints->ai_protocol : 0;
    r->ai.ai_addrlen = sizeof(r->sin);
    r->ai.ai_addr = (struct sockaddr *) &r->sin;

    *res = &r->ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    /* one result, allocated with its address */
    free(res);
}

const char *gai_strerror(int err)
{
    switch (err) {
        case 0: return "Success";
        case EAI_BADFLAGS: return "Invalid flags";
        case EAI_NONAME: return "Name or service not known";
        case EAI_AGAIN: return "Temporary failure in name resolution";
        case EAI_FAIL: return "Non-recoverable failure in name resolution";
        case EAI_FAMILY: return "Address family not supported";
        case EAI_SOCKTYPE: return "Socket type not supported";
        case EAI_SERVICE: return "Service not supported for socket type";
        case EAI_MEMORY: return "Memory allocation failure";
        case EAI_SYSTEM: return "System error";
        case EAI_OVERFLOW: return "Argument buffer overflow";
    }

    return "Unknown error";
}

int getnameinfo(const struct sockaddr *addr, socklen_t addrlen,
                char *host, socklen_t hostlen, char *serv, socklen_t servlen,
                int flags)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *) addr;

    if (addr->sa_family != AF_INET || addrlen < sizeof(*sin))
        return EAI_FAMILY;

    /* numeric only, there are no names */
    if (flags & NI_NAMEREQD)
        return EAI_NONAME;

    if (host && hostlen && !inet_ntop(AF_INET, &sin->sin_addr, host, hostlen))
        return EAI_OVERFLOW;

    if (serv && servlen &&
        snprintf(serv, servlen, "%u", ntohs(sin->sin_port)) >= (int) servlen)
        return EAI_OVERFLOW;

    return 0;
}

unsigned int if_nametoindex(const char *name)
{
    (void) name;
    errno = ENXIO;
    return 0;
}

char *if_indextoname(unsigned int index, char *name)
{
    (void) index; (void) name;
    errno = ENXIO;
    return NULL;
}

struct if_nameindex *if_nameindex(void)
{
    return calloc(1, sizeof(struct if_nameindex));
}

void if_freenameindex(struct if_nameindex *ptr)
{
    free(ptr);
}

int getifaddrs(struct ifaddrs **ifap)
{
    *ifap = NULL;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    (void) ifa;
}
