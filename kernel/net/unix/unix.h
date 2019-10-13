#ifndef _NET_UNIX_H
#define _NET_UNIX_H

#define AF_UNIX_CONN    255

struct un_conn {
    struct ringbuf *soci;   /* Server Out, Client In */
    struct ringbuf *sico;   /* Server In, Client Out */

    struct queue server_recv;
    struct queue server_send;
    struct queue client_recv;
    struct queue client_send;
    struct queue connect;

    int connected;
};

struct un_socket {
    struct vnode *vnode;

    size_t backlog;    /* Number of connections to keep in queue */

    struct queue requests;
    struct queue accept;

    /* Flags */
    int listening;
};

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[128];
};

#endif /* ! _NET_UNIX_H */
