/*
 * Host and service names (the resolver is resolv.c: /etc/hosts, then the
 * DNS), protocols, and network interfaces (none listed yet)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "resolver.h"

#define MAX_ADDRS   16

int h_errno;

static int h_error(int eai)
{
    switch (eai) {
        case EAI_AGAIN:  return TRY_AGAIN;
        case EAI_FAIL:   return NO_RECOVERY;
        default:         return HOST_NOT_FOUND;
    }
}

static struct hostent host;
static char host_name[256];
static char *host_aliases[1];
static struct in_addr host_addrs[MAX_ADDRS];
static char *host_list[MAX_ADDRS + 1];

static struct hostent *host_fill(int n)
{
    for (int i = 0; i < n; ++i)
        host_list[i] = (char *) &host_addrs[i];

    host_list[n] = NULL;
    host_aliases[0] = NULL;

    host.h_name      = host_name;
    host.h_aliases   = host_aliases;
    host.h_addrtype  = AF_INET;
    host.h_length    = 4;
    host.h_addr_list = host_list;
    return &host;
}

struct hostent *gethostbyname(const char *name)
{
    int n = 0, err;

    if (!name) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }

    if (inet_aton(name, &host_addrs[0])) {
        inet_ntop(AF_INET, &host_addrs[0], host_name, sizeof(host_name));
        return host_fill(1);
    }

    if ((err = __resolve(name, host_addrs, MAX_ADDRS, &n, host_name, sizeof(host_name)))) {
        h_errno = h_error(err);
        return NULL;
    }

    return host_fill(n);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    int err;

    if (type != AF_INET || len < 4) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }

    memcpy(&host_addrs[0], addr, 4);

    if ((err = __resolve_reverse(host_addrs[0], host_name, sizeof(host_name)))) {
        h_errno = h_error(err);
        return NULL;
    }

    return host_fill(1);
}

/*
 * /etc/services: "name port/protocol aliases..."
 */

static struct servent serv;
static char serv_name[64], serv_proto[16];
static char *serv_aliases[1];

static struct servent *services(const char *name, int port, const char *proto)
{
    FILE *f = fopen("/etc/services", "r");
    char line[256];
    struct servent *found = NULL;

    while (f && !found && fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');

        if (hash)
            *hash = '\0';

        char *s = strtok(line, " \t\r\n");
        char *pp = strtok(NULL, " \t\r\n");
        char *slash = pp ? strchr(pp, '/') : NULL;

        if (!s || !slash)
            continue;

        *slash = '\0';
        int p = atoi(pp);
        const char *pr = slash + 1;

        if (proto && strcmp(proto, pr))
            continue;

        int match = name ? !strcmp(s, name) : p == port;

        for (char *a; name && !match && (a = strtok(NULL, " \t\r\n")); )
            match = !strcmp(a, name);

        if (match) {
            snprintf(serv_name, sizeof(serv_name), "%s", s);
            snprintf(serv_proto, sizeof(serv_proto), "%s", pr);
            serv_aliases[0] = NULL;
            serv.s_name    = serv_name;
            serv.s_aliases = serv_aliases;
            serv.s_port    = htons(p);
            serv.s_proto   = serv_proto;
            found = &serv;
        }
    }

    if (f)
        fclose(f);

    return found;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    return name ? services(name, 0, proto) : NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
    return services(NULL, ntohs(port), proto);
}

struct protoent *getprotobyname(const char *name)
{
    static struct {
        const char *name;
        int proto;
    } protos[] = {{"ip", 0}, {"icmp", 1}, {"tcp", 6}, {"udp", 17}};
    static struct protoent p;
    static char *aliases[1];

    for (size_t i = 0; name && i < sizeof(protos) / sizeof(protos[0]); ++i) {
        if (!strcmp(name, protos[i].name)) {
            p.p_name    = (char *) protos[i].name;
            p.p_aliases = aliases;
            p.p_proto   = protos[i].proto;
            return &p;
        }
    }

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
    int socktype = hints ? hints->ai_socktype : 0;
    struct in_addr addrs[MAX_ADDRS];
    char canon[256] = "";
    int naddrs = 1, port = 0, err;

    if (!node && !service)
        return EAI_NONAME;

    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;

    if (service && (port = port_number(service)) < 0) {
        struct servent *s;

        if (flags & AI_NUMERICSERV)
            return EAI_NONAME;

        if (!(s = getservbyname(service, socktype == SOCK_DGRAM ? "udp" : "tcp")))
            return EAI_SERVICE;

        port = ntohs(s->s_port);
    }

    if (!node) {
        addrs[0].s_addr = htonl(flags & AI_PASSIVE ? INADDR_ANY : INADDR_LOOPBACK);
    } else if (inet_aton(node, &addrs[0])) {
        snprintf(canon, sizeof(canon), "%s", node);
    } else if (flags & AI_NUMERICHOST) {
        return EAI_NONAME;
    } else if ((err = __resolve(node, addrs, MAX_ADDRS, &naddrs, canon, sizeof(canon)))) {
        return err;
    }

    /* an entry for each address, its sockaddr with it */
    struct entry {
        struct addrinfo ai;
        struct sockaddr_in sin;
    };

    struct addrinfo *head = NULL, **tail = &head;

    for (int i = 0; i < naddrs; ++i) {
        struct entry *e = calloc(1, sizeof(*e));

        if (!e) {
            freeaddrinfo(head);
            return EAI_MEMORY;
        }

        e->sin.sin_family = AF_INET;
        e->sin.sin_port = htons(port);
        e->sin.sin_addr = addrs[i];

        e->ai.ai_family = AF_INET;
        e->ai.ai_socktype = socktype;
        e->ai.ai_protocol = hints ? hints->ai_protocol : 0;
        e->ai.ai_addrlen = sizeof(e->sin);
        e->ai.ai_addr = (struct sockaddr *) &e->sin;

        if (!i && (flags & AI_CANONNAME) && node)
            e->ai.ai_canonname = strdup(canon[0] ? canon : node);

        *tail = &e->ai;
        tail = &e->ai.ai_next;
    }

    *res = head;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    /* each allocated with its address */
    while (res) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_canonname);
        free(res);
        res = next;
    }
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

    if (host && hostlen) {
        int named = !(flags & NI_NUMERICHOST) &&
                    !__resolve_reverse(sin->sin_addr, host, hostlen);

        if (!named) {
            if (flags & NI_NAMEREQD)
                return EAI_NONAME;

            if (!inet_ntop(AF_INET, &sin->sin_addr, host, hostlen))
                return EAI_OVERFLOW;
        }
    }

    if (serv && servlen) {
        struct servent *s = flags & NI_NUMERICSERV ? NULL :
                            getservbyport(sin->sin_port, flags & NI_DGRAM ? "udp" : "tcp");

        if (snprintf(serv, servlen, "%s", s ? s->s_name : "") >= (int) servlen ||
            (!s && snprintf(serv, servlen, "%u", ntohs(sin->sin_port)) >= (int) servlen))
            return EAI_OVERFLOW;
    }

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
