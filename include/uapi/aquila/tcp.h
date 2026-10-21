#ifndef _AQUILA_TCP_H
#define _AQUILA_TCP_H

/* TCP's socket options (IPPROTO_TCP), shared by the kernel and
 * <netinet/tcp.h>; the kernel has TCP_NODELAY and TCP_MAXSEG (which it
 * only reports) */

#define TCP_NODELAY     1       /* no Nagle */
#define TCP_MAXSEG      2       /* the segments' data, at most */
#define TCP_KEEPIDLE    4
#define TCP_KEEPINTVL   5
#define TCP_KEEPCNT     6

#endif /* ! _AQUILA_TCP_H */
