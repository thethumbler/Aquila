/* the resolver (resolv.c), for netdb.c */

#ifndef _AQUILA_RESOLVER_H
#define _AQUILA_RESOLVER_H

#include <stddef.h>
#include <netinet/in.h>

/* `name's IPv4 addresses (at most max) and its canonical name: 0, or an
 * EAI_* error */
int __resolve(const char *name, struct in_addr *addrs, int max, int *naddrs,
              char *canon, size_t canonlen);

/* the name of an address: 0, or an EAI_* error */
int __resolve_reverse(struct in_addr addr, char *name, size_t len);

#endif
