#include <core/system.h>
#include <dev/dev.h>
#include <fs/posix.h>
#include <aquila/errno.h>

#include <memdev.h>

struct dev nulldev;

static ssize_t nulldev_read(struct devid *dd __unused, off_t offset __unused, struct iobuf *io __unused)
{
    return 0;
}

/** taken, unread */
static ssize_t nulldev_write(struct devid *dd __unused, off_t offset __unused, struct iobuf *io)
{
    return iobuf_skip(io, io->resid);
}

struct dev nulldev = {
    .read  = nulldev_read,
    .write = nulldev_write,

    .fops  = {
        .open  = posix_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,
        .close = posix_file_close,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_always,
        .eof       = __vfs_eof_always,
    },
};
