/** \file
 * System calls: sockets: making, binding, connecting, sending and
 * receiving, their names and options
 */

#include "syscalls.h"

long sys_socket(int domain, int type, int protocol)
{
    syscall_log(LOG_DEBUG, "socket(domain=%d, type=%d, protocol=%d)\n",
            domain, type, protocol);

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        return -EMFILE;
    }
    
    int err = 0;
    struct file *file = file_new();

    if (!file || (err = socket_create(file, domain, type, protocol))) {
        /* not open: nothing to close */
        kfree(file);
        proc_fd_release(curproc, fd);
        return file ? err : -ENOMEM;
    }

    proc_fd_install(curproc, fd, file, 0);
    return fd;
}

/**
 * Socket addresses: the kernel's copy, as big as the biggest (AF_UNIX's).
 * One coming in is len bytes of user space's; one going out is the
 * protocol's (*klen its size), as much of it as fits in user space's
 * (*ulen bytes), *ulen then its size.
 */
union ksockaddr {
    struct sockaddr sa;
    struct sockaddr_un un;
};

static int sockaddr_copyin(union ksockaddr *k, const struct sockaddr *uaddr, socklen_t len)
{
    if (len > sizeof(*k))
        return -EINVAL;

    return copyin(k, uaddr, len);
}

/** user space's room for one going out: its size, at most ours */
static int sockaddr_room(socklen_t *ulen, socklen_t *klen)
{
    if (copyin(klen, ulen, sizeof(*klen)))
        return -EFAULT;

    *klen = MIN(*klen, sizeof(union ksockaddr));
    return 0;
}

static int sockaddr_copyout(struct sockaddr *uaddr, socklen_t *ulen, const union ksockaddr *k, socklen_t klen, socklen_t room)
{
    if (copyout(uaddr, k, MIN(klen, room)) || copyout(ulen, &klen, sizeof(klen)))
        return -EFAULT;

    return 0;
}

long sys_accept(int fd, const struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "accept(fd=%d, addr=%p, len=%p)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        return -EBADF;
    }

    int conn_fd = proc_fd_get(curproc);

    if (conn_fd == -1) {
        return -EMFILE;
    }

    struct file *conn = file_new();

    if (!conn) {
        proc_fd_release(curproc, conn_fd);
        return -ENOMEM;
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    socklen_t room = 0, klen = 0;
    int err = 0;

    if (addr && (err = sockaddr_room(len, &room))) {
        kfree(conn);
        proc_fd_release(curproc, conn_fd);
        return err;
    }

    klen = room;

    if ((err = socket_accept(socket, conn, addr ? &k.sa : NULL, addr ? &klen : NULL))) {
        kfree(conn);
        proc_fd_release(curproc, conn_fd);
        return err; 
    }

    proc_fd_install(curproc, conn_fd, conn, 0);

    /* nowhere to say who: the connection goes */
    if (addr && sockaddr_copyout((struct sockaddr *) addr, len, &k, klen, room)) {
        proc_fd_close(curproc, conn_fd);
        return -EFAULT;
    }

    return conn_fd;
}

long sys_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    syscall_log(LOG_DEBUG, "bind(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    int err = 0;

    if ((err = sockaddr_copyin(&k, addr, len)) || (err = socket_bind(file, &k.sa, len))) {
        return err; 
    }

    return 0;
}

long sys_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    syscall_log(LOG_DEBUG, "connect(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        return -EBADF; 
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    int err = 0;

    if ((err = sockaddr_copyin(&k, addr, len)) || (err = socket_connect(socket, &k.sa, len))) {
        return err; 
    }

    return 0;
}

long sys_listen(int fd, int backlog)
{
    syscall_log(LOG_DEBUG, "listen(fd=%d, backlog=%d)\n", fd, backlog);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if ((err = socket_listen(file, backlog))) {
        return err; 
    }

    return 0;
}

long sys_send(struct aquila_socket_io_args *us)
{
    struct aquila_socket_io_args _s, *s = &_s;

    if (copyin(s, us, sizeof(*s))) {
        return -EFAULT;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "send(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* sendto(): where to */
    union ksockaddr to;
    memset(&to, 0, sizeof(to));
    socklen_t tolen = 0;

    if (s->addr) {
        if (copyin(&tolen, s->addrlen, sizeof(tolen))) {
            return -EFAULT;
        }

        if ((err = sockaddr_copyin(&to, s->addr, tolen))) {
            return err;
        }
    }

    struct iobuf io;
    iobuf_init(&io, buf, len, 1);

    ssize_t ret = socket_send(file, &io, flags, s->addr ? &to.sa : NULL, tolen);

    /* to a connection that can't send anymore: SIGPIPE too, as write()
     * (not with MSG_NOSIGNAL) */
    if (ret == -EPIPE && !(flags & MSG_NOSIGNAL))
        signal_proc_send(curproc, SIGPIPE);

    return ret;
}

long sys_recv(struct aquila_socket_io_args *us)
{
    struct aquila_socket_io_args _s, *s = &_s;

    if (copyin(s, us, sizeof(*s))) {
        return -EFAULT;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "recv(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* recvfrom(): where from */
    union ksockaddr from;
    memset(&from, 0, sizeof(from));
    socklen_t room = 0, fromlen = 0;

    if (s->addr && (err = sockaddr_room(s->addrlen, &room))) {
        return err;
    }

    fromlen = room;

    struct iobuf io;
    iobuf_init(&io, buf, len, 1);

    ssize_t ret = socket_recv(file, &io, flags, s->addr ? &from.sa : NULL, s->addr ? &fromlen : NULL);

    if (ret >= 0 && s->addr && sockaddr_copyout(s->addr, s->addrlen, &from, fromlen, room))
        ret = -EFAULT;

    return ret;
}

/** getsockname(), getpeername() */
static long socket_name(int fd, struct sockaddr *addr, socklen_t *len, int peer)
{
    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    socklen_t room, klen;
    int err;

    if ((err = sockaddr_room(len, &room))) {
        return err;
    }

    klen = room;

    if (!(err = socket_getname(file, &k.sa, &klen, peer)))
        err = sockaddr_copyout(addr, len, &k, klen, room);

    return err;
}

long sys_getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getsockname(fd=%d, addr=%p, len=%p)\n", fd, addr, len);
    return socket_name(fd, addr, len, 0);
}

long sys_getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getpeername(fd=%d, addr=%p, len=%p)\n", fd, addr, len);
    return socket_name(fd, addr, len, 1);
}

long sys_getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getsockopt(fd=%d, level=%d, name=%d, val=%p, len=%p)\n",
            fd, level, name, val, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    /* an option's value: the kernel's copy (the biggest is a timeval) */
    char kval[64];
    memset(kval, 0, sizeof(kval));
    socklen_t room, klen;
    int err;

    if (copyin(&room, len, sizeof(room))) {
        return -EFAULT;
    }

    klen = room = MIN(room, sizeof(kval));

    if (!(err = socket_getsockopt(file, level, name, kval, &klen)) &&
        (copyout(val, kval, MIN(klen, room)) || copyout(len, &klen, sizeof(klen))))
        err = -EFAULT;

    return err;
}

long sys_setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    syscall_log(LOG_DEBUG, "setsockopt(fd=%d, level=%d, name=%d, val=%p, len=%d)\n",
            fd, level, name, val, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    char kval[64];

    len = MIN(len, sizeof(kval));

    if (copyin(kval, val, len)) {
        return -EFAULT;
    }

    return socket_setsockopt(file, level, name, kval, len);
}

long sys_shutdown(int fd, int how)
{
    syscall_log(LOG_DEBUG, "shutdown(fd=%d, how=%d)\n", fd, how);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    return socket_shutdown_dir(file, how);
}
