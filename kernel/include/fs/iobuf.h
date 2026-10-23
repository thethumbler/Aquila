#ifndef _FS_IOBUF_H
#define _FS_IOBUF_H

/*
 * An iobuf: the memory a read fills or a write empties, as it moves
 * through the VFS, the devices and the sockets
 *
 * A system call's buffer is user space's; the kernel's own reads and
 * writes (a filesystem reading its device, execve reading a file) are
 * the kernel's. Whoever moves the bytes doesn't need to know which:
 * iobuf_out() puts bytes in the buffer, iobuf_in() takes them out, both
 * from where the last one stopped, and a user buffer goes through
 * copyout()/copyin() (sys/uaccess.h), which return -EFAULT where the
 * memory turns out not to be there.
 *
 * The buffer may be several pieces (segments, as readv's vectors would
 * be), taken in order; resid is what's left of them. A read returns how
 * much it filled (the resid it started with, less the resid it ends
 * with), a write how much it emptied. A fault ends the transfer (fault
 * is set, a loop waiting for more stops): what moved before it counts,
 * and if nothing did, the call returns -EFAULT.
 */

#include <core/system.h>

struct iobuf_seg {
    char    *base;
    size_t  len;
};

/** passed by its address, never copied: seg may point at its own `one' */
struct iobuf {
    struct iobuf_seg *seg;      /* the segments left (the first partly done) */
    int     nseg;
    size_t  resid;              /* bytes left in them */
    int     user;               /* user space's memory */
    int     fault;              /* a copy faulted: the transfer is over */
    struct iobuf_seg one;       /* a single buffer's segment */
};

/* a single buffer: len bytes at buf, user space's if `user' */
void    iobuf_init(struct iobuf *io, void *buf, size_t len, int user);

/* n bytes from the kernel's src into the buffer (at most resid): how
 * many (fewer: a fault stopped it), -EFAULT if a fault came first */
ssize_t iobuf_out(struct iobuf *io, const void *src, size_t n);

/* n bytes from the buffer into the kernel's dst (at most resid): how
 * many (fewer: a fault stopped it), -EFAULT if a fault came first */
ssize_t iobuf_in(struct iobuf *io, void *dst, size_t n);

/* as iobuf_in(), but the buffer stays where it is: what the kernel
 * takes of it is then passed over with iobuf_skip() */
ssize_t iobuf_peek(struct iobuf *io, void *dst, size_t n);

/* n zero bytes into the buffer (at most resid): as iobuf_out() */
ssize_t iobuf_zero(struct iobuf *io, size_t n);

/* n bytes of the buffer passed over, untouched (a write that's
 * dropped): how many (at most resid) */
size_t  iobuf_skip(struct iobuf *io, size_t n);

/** the buffer cut to n bytes for a while (what's passed on may move no
 * more): what was cut, given back with iobuf_unlimit() */
static inline size_t iobuf_limit(struct iobuf *io, size_t n)
{
    size_t cut = io->resid > n ? io->resid - n : 0;
    io->resid -= cut;
    return cut;
}

static inline void iobuf_unlimit(struct iobuf *io, size_t cut)
{
    io->resid += cut;
}

#endif /* ! _FS_IOBUF_H */
