#ifndef _FS_IOBUF_H
#define _FS_IOBUF_H

/*
 * An iobuf is the memory that a read fills or a write empties, as it
 * moves through the VFS, the devices and the sockets (fs/iobuf.c).
 *
 * A system call's buffer is in user memory; the kernel's own reads and
 * writes (a filesystem reading its device, execve() reading a file) use
 * kernel memory. Whoever moves the bytes doesn't need to know which:
 * iobuf_out() puts bytes in the buffer and iobuf_in() takes them out,
 * each from where the last one stopped, and a user buffer goes through
 * copyout() and copyin() (sys/uaccess.h), which return -EFAULT where the
 * memory turns out not to be there.
 *
 * The buffer may be in several pieces (segments, as readv()'s vectors
 * would be), taken in order; `resid` is how much is left of them. A read
 * returns how much it filled (the resid it started with, less the resid
 * it ends with), and a write how much it emptied. A fault ends the
 * transfer: `fault` is set, so that a loop waiting for more stops. What
 * moved before the fault counts, and if nothing did, the call returns
 * -EFAULT.
 */

#include <core/system.h>

/** A piece of an iobuf's memory. */
struct iobuf_seg {
    char    *base;
    size_t  len;
};

/** An iobuf. It is passed by its address and never copied, because `seg`
 * may point at its own `one`. */
struct iobuf {
    struct iobuf_seg *seg;      /* the segments left (the first may be partly done) */
    int     nseg;
    size_t  resid;              /* bytes left in them */
    int     user;               /* whether it's user memory */
    int     fault;              /* a copy faulted, so the transfer is over */
    struct iobuf_seg one;       /* a single buffer's segment */
};

/* Sets up an iobuf of one buffer: `len` bytes at `buf`, in user memory
 * if `user` is set. */
void    iobuf_init(struct iobuf *io, void *buf, size_t len, int user);

/* Copies up to `n` bytes (at most resid) from the kernel's `src` into the
 * buffer. Returns how many (fewer if a fault stopped it), or -EFAULT if a
 * fault came first. */
ssize_t iobuf_out(struct iobuf *io, const void *src, size_t n);

/* Copies up to `n` bytes (at most resid) from the buffer into the
 * kernel's `dst`. Returns how many (fewer if a fault stopped it), or
 * -EFAULT if a fault came first. */
ssize_t iobuf_in(struct iobuf *io, void *dst, size_t n);

/* Copies from the buffer as iobuf_in() does, but without moving on: the
 * caller then passes over what it took with iobuf_skip(). */
ssize_t iobuf_peek(struct iobuf *io, void *dst, size_t n);

/* Writes up to `n` zero bytes into the buffer, as iobuf_out() does. */
ssize_t iobuf_zero(struct iobuf *io, size_t n);

/* Passes over up to `n` bytes of the buffer, untouched (a write that's
 * thrown away). Returns how many (at most resid). */
size_t  iobuf_skip(struct iobuf *io, size_t n);

/** Cuts the buffer to `n` bytes for a while, so that what it's passed to
 * moves no more. Returns how much was cut, which iobuf_unlimit() gives
 * back. */
static inline size_t iobuf_limit(struct iobuf *io, size_t n)
{
    size_t cut = io->resid > n ? io->resid - n : 0;
    io->resid -= cut;
    return cut;
}

/** Gives back what iobuf_limit() cut. */
static inline void iobuf_unlimit(struct iobuf *io, size_t cut)
{
    io->resid += cut;
}

#endif /* ! _FS_IOBUF_H */
