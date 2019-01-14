#include <core/system.h>
#include <dev/dev.h>
#include <fs/posix.h>
#include <bits/errno.h>

#include <memdev.h>

struct dev nulldev;

static ssize_t nulldev_read(struct devid *dd __unused, off_t offset __unused, size_t size __unused, void *buf __unused)
{
    return 0;
}

static ssize_t nulldev_write(struct devid *dd __unused, off_t offset __unused, size_t size, void *buf __unused)
{
    return size;
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
