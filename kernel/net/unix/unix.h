#ifndef _NET_UNIX_H
#define _NET_UNIX_H

#define AF_UNIX_CONN    255

struct un_conn {
    struct ringbuf *soci;   /* Server Out, Client In */
    struct ringbuf *sico;   /* Server In, Client Out */

    /* the writer of the ring is done (shutdown, close): what is left is
     * read, then the end; the reader frees the ring */
    int soci_end, sico_end;

    struct waitq server_recv;
    struct waitq server_send;
    struct waitq client_recv;
    struct waitq client_send;
    struct waitq connect;

    /* 1 when accepted, -1 when refused by the listener closing */
    int connected;
};

struct un_socket {
    struct vnode *vnode;

    size_t backlog;    /* Number of connections to keep in queue */

    struct queue requests;
    struct waitq accept;

    /* Flags */
    int listening;
};

#include <aquila/un.h>

#endif /* ! _NET_UNIX_H */
