#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec {
    void   *iov_base;
    size_t  iov_len;
};

#define IOV_MAX 1024

/* one read() or write() (a buffer gathers the vectors, the kernel has no
 * vectored I/O) */
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* ! _SYS_UIO_H */
