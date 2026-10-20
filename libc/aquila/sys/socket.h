#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

/* address families, socket types and struct sockaddr, shared with the kernel */
#include <aquila/socket.h>

int socket(int domain, int type, int protocol);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#endif /* ! _SYS_SOCKET_H */
