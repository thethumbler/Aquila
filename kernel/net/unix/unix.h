#ifndef _NET_UNIX_H
#define _NET_UNIX_H

#define AF_UNIX_CONN    255

struct un_conn {
    struct ringbuf *soci;   /* Server Out, Client In */
    struct ringbuf *sico;   /* Server In, Client Out */

    queue_t server_recv_sleep;
    queue_t server_send_sleep;
    queue_t client_recv_sleep;
    queue_t client_send_sleep;

    queue_t connect_sleep;

    int connected : 1;
};

struct un_socket {
    struct vnode vnode;     /* Identifier -- FIXME */

    size_t backlog;    /* Number of connections to keep in queue */

    queue_t requests;
    queue_t accept_sleep;

    /* Flags */
    int listening : 1;
};

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[128];
};

#endif /* ! _NET_UNIX_H */
