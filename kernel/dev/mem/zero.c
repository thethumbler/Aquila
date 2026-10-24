/** \file
 * `/dev/zero` (character device 1, 5): reading it gives as many zero
 * bytes as asked for, and what is written to it is thrown away.
 */

#include <core/system.h>
#include <dev/dev.h>
#include <fs/posix.h>
#include <aquila/errno.h>

#include <memdev.h>

struct dev zerodev;

/** Fills the reader's buffer with zeros. */
static ssize_t zerodev_read(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    return iobuf_zero(io, io->resid);
}

/** Takes all that is written, and throws it away. */
static ssize_t zerodev_write(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    return iobuf_skip(io, io->resid);
}

struct dev zerodev = {
    .read  = zerodev_read,
    .write = zerodev_write,

    .fops  = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .close = posix_file_close,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_never,
    },
};
