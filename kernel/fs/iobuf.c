/*
 * iobuf: moving bytes into and out of a read's or a write's buffer
 * (fs/iobuf.h)
 */

#include <core/system.h>
#include <core/string.h>
#include <fs/iobuf.h>
#include <sys/uaccess.h>

void iobuf_init(struct iobuf *io, void *buf, size_t len, int user)
{
    io->one   = (struct iobuf_seg) {.base = buf, .len = len};
    io->seg   = &io->one;
    io->nseg  = 1;
    io->resid = len;
    io->user  = user;
    io->fault = 0;
}

/*
 * Up to n bytes between the buffer and the kernel's kbuf (out: into the
 * buffer), from where the buffer is, segment by segment: how many (fewer:
 * a fault stopped it), -EFAULT if a fault came first. The buffer isn't
 * moved past them.
 */
static ssize_t iobuf_copy(struct iobuf *io, char *kbuf, size_t n, int out)
{
    size_t done = 0;

    if (io->fault)
        return -EFAULT;

    n = MIN(n, io->resid);

    for (int i = 0; i < io->nseg && done < n; ++i) {
        char *p = io->seg[i].base;
        size_t len = MIN(n - done, io->seg[i].len);

        if (!len)
            continue;

        if (!io->user) {
            if (out)
                memcpy(p, kbuf + done, len);
            else
                memcpy(kbuf + done, p, len);
        } else {
            size_t n = out ? copyout_upto(p, kbuf + done, len) : copyin_upto(kbuf + done, p, len);

            /* what went before the fault counts */
            if (n < len) {
                io->fault = 1;
                done += n;
                return done ? (ssize_t) done : -EFAULT;
            }
        }

        done += len;
    }

    return done;
}

size_t iobuf_skip(struct iobuf *io, size_t n)
{
    size_t done = 0;

    n = MIN(n, io->resid);

    while (done < n) {
        size_t len = MIN(n - done, io->seg->len);

        io->seg->base += len;
        io->seg->len  -= len;
        done += len;

        /* the next segment */
        if (!io->seg->len && io->nseg > 1) {
            io->seg++;
            io->nseg--;
        }
    }

    io->resid -= done;
    return done;
}

ssize_t iobuf_out(struct iobuf *io, const void *src, size_t n)
{
    ssize_t r = iobuf_copy(io, (char *) src, n, 1);

    if (r > 0)
        iobuf_skip(io, r);

    return r;
}

ssize_t iobuf_in(struct iobuf *io, void *dst, size_t n)
{
    ssize_t r = iobuf_copy(io, dst, n, 0);

    if (r > 0)
        iobuf_skip(io, r);

    return r;
}

ssize_t iobuf_peek(struct iobuf *io, void *dst, size_t n)
{
    return iobuf_copy(io, dst, n, 0);
}

ssize_t iobuf_zero(struct iobuf *io, size_t n)
{
    static const char zero[256];
    size_t done = 0;

    n = MIN(n, io->resid);

    while (done < n) {
        size_t len = MIN(n - done, sizeof(zero));
        ssize_t r = iobuf_out(io, zero, len);

        if (r < 0)
            return done ? (ssize_t) done : r;

        done += r;

        /* a fault stopped it */
        if ((size_t) r < len)
            break;
    }

    return done;
}
