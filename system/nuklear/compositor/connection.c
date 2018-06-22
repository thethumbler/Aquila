#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define _POSIX_PTHREAD
#include <pthread.h>

#define NKUI_SOCKET   "/tmp/nkui"
#define MAX_CLIENTS 20

static int socket_fd;
static pthread_t conn_tid;

void *connection_worker(void *arg)
{
    for (;;) {
        int fd = accept(socket_fd, NULL, NULL);

        /* Spawn a new worker */
        pthread_t tid;
        extern void *client_worker(void *);
        pthread_create(&tid, NULL, client_worker, &fd); /* FIXME */
    }
}

pthread_t q[10];
int connection_init()
{
    int s;
    struct sockaddr_un addr;

    char path[100];

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    /* Set socket address */
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, NKUI_SOCKET);
    size_t len = strlen(addr.sun_path) + sizeof(addr.sun_family);

    /* Unlink file if present */
    unlink(addr.sun_path);

    if (bind(s, (struct sockaddr *) &addr, len) == -1) {
        perror("bind");
        return -1;
    }

    if (listen(s, MAX_CLIENTS) == -1) {
        perror("listen");
        return -1;
    }

    socket_fd = s;
    pthread_create(&conn_tid, NULL, connection_worker, NULL);

    return s;
}
