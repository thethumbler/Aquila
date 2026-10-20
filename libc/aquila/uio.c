/*
 * readv() and writev(): the kernel has no vectored I/O, the vectors go
 * through a buffer and one read() or write(), so that a write to a socket
 * or a pipe stays whole as it would.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

/* the total length, -1 if the vectors are invalid */
static ssize_t iov_length(const struct iovec *iov, int iovcnt)
{
    size_t len = 0;

    if (iovcnt < 0 || iovcnt > IOV_MAX)
        return -1;

    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len > SSIZE_MAX - len)
            return -1;
        len += iov[i].iov_len;
    }

    return len;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t len = iov_length(iov, iovcnt);

    if (len < 0) {
        errno = EINVAL;
        return -1;
    }

    if (iovcnt == 1)
        return write(fd, iov[0].iov_base, iov[0].iov_len);

    char *buf = malloc(len ? len : 1), *p = buf;

    if (!buf)
        return -1;

    for (int i = 0; i < iovcnt; ++i) {
        memcpy(p, iov[i].iov_base, iov[i].iov_len);
        p += iov[i].iov_len;
    }

    ssize_t ret = write(fd, buf, len);
    int err = errno;

    free(buf);
    errno = err;
    return ret;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t len = iov_length(iov, iovcnt);

    if (len < 0) {
        errno = EINVAL;
        return -1;
    }

    if (iovcnt == 1)
        return read(fd, iov[0].iov_base, iov[0].iov_len);

    char *buf = malloc(len ? len : 1), *p = buf;

    if (!buf)
        return -1;

    ssize_t ret = read(fd, buf, len);

    for (int i = 0; ret > 0 && i < iovcnt && p < buf + ret; ++i) {
        size_t n = buf + ret - p;
        n = n < iov[i].iov_len ? n : iov[i].iov_len;
        memcpy(iov[i].iov_base, p, n);
        p += n;
    }

    int err = errno;
    free(buf);
    errno = err;
    return ret;
}
