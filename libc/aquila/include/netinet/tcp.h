#ifndef _NETINET_TCP_H
#define _NETINET_TCP_H

/* TCP socket options (IPPROTO_TCP), for programs that know of them: the
 * kernel has no TCP yet */

#define TCP_NODELAY     1
#define TCP_MAXSEG      2
#define TCP_KEEPIDLE    4
#define TCP_KEEPINTVL   5
#define TCP_KEEPCNT     6

#endif /* ! _NETINET_TCP_H */
