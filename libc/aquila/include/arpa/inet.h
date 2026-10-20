#ifndef _ARPA_INET_H
#define _ARPA_INET_H

/* Internet address types (<netinet/in.h>), byte order, and conversions
 * between addresses and their text (IPv4 only) */

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t   inet_addr(const char *cp);
int         inet_aton(const char *cp, struct in_addr *inp);
char       *inet_ntoa(struct in_addr in);
int         inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

#ifdef __cplusplus
}
#endif

#endif /* ! _ARPA_INET_H */
