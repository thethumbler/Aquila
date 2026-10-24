/** \file
 * The operations on open files: each goes to the socket layer, the
 * device's driver, or the vnode's filesystem (its `fops`).
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/pipe.h>
#include <dev/dev.h>
#include <net/socket.h>
#include <aquila/fcntl.h>

/**
 * Opens a file whose vnode is set: a device's driver, a FIFO's pipe, or
 * the filesystem opens it.
 *
 * @param file  the file, its vnode and flags set
 * @return 0, `-EISDIR` to write to a directory, `-ENOSYS`, or the open's
 *         error
 */
int vfs_file_open(struct file *file)
{
    if (!file || !file->vnode || !file->vnode->fs)
        return -EINVAL;

    /* directories are only read (with readdir) */
    if (S_ISDIR(file->vnode->mode) && (file->flags & (O_WRONLY | O_RDWR)))
        return -EISDIR;

    if (ISDEV(file->vnode))
        return kdev_file_open(&VNODE_DEV(file->vnode), file);

    /* a FIFO's open file becomes an end of its pipe */
    if (S_ISFIFO(file->vnode->mode))
        return pipefs_fifo_open(file);

    if (!file->vnode->fs->fops.open)
        return -ENOSYS;

    return file->vnode->fs->fops.open(file);
}

/**
 * Reads from an open file, at its offset.
 *
 * @param file  the open file
 * @param io    where the data goes, and how much is wanted
 * @return the number of bytes read, 0 at the end, `-EISDIR` for a
 *         directory, or a negative errno
 */
ssize_t vfs_file_read(struct file *file, struct iobuf *io)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_recv(file, io, 0, NULL, NULL);

    if (!file || !file->vnode)
        return -EINVAL;

    /* directories are read with readdir */
    if (S_ISDIR(file->vnode->mode))
        return -EISDIR;

    if (ISDEV(file->vnode))
        return kdev_file_read(&VNODE_DEV(file->vnode), file, io);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.read)
        return -ENOSYS;

    return file->vnode->fs->fops.read(file, io);
}

/**
 * Writes to an open file, at its offset.
 *
 * @param file  the open file
 * @param io    the data, and how much of it
 * @return the number of bytes written, or a negative errno
 */
ssize_t vfs_file_write(struct file *file, struct iobuf *io)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_send(file, io, 0, NULL, 0);

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_write(&VNODE_DEV(file->vnode), file, io);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.write)
        return -ENOSYS;

    return file->vnode->fs->fops.write(file, io);
}

/**
 * Passes an `ioctl()` request to an open file's device or filesystem.
 *
 * @param file     the open file
 * @param request  the request
 * @param argp     its argument
 * @return the request's result, or a negative errno
 */
int vfs_file_ioctl(struct file *file, int request, void *argp)
{
    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_ioctl(&VNODE_DEV(file->vnode), file, request, argp);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.ioctl)
        return -ENOSYS;

    return file->vnode->fs->fops.ioctl(file, request, argp);
}

/**
 * Moves an open file's offset.
 *
 * @param file    the open file
 * @param offset  the offset, or the distance to move it
 * @param whence  `SEEK_SET`, `SEEK_CUR` or `SEEK_END`
 * @return the new offset, `-ESPIPE` for a pipe, or a negative errno
 */
off_t vfs_file_lseek(struct file *file, off_t offset, int whence)
{
    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_lseek(&VNODE_DEV(file->vnode), file, offset, whence);

    if (!file->vnode->fs)
        return -EINVAL;

    /* pipes can't seek */
    if (!file->vnode->fs->fops.lseek)
        return -ESPIPE;

    return file->vnode->fs->fops.lseek(file, offset, whence);
}

/**
 * Reads the next entry of an open directory.
 *
 * @param file    the open directory
 * @param dirent  the entry, filled in
 * @return 1 if an entry was read, 0 at the end, `-ENOTDIR`, or a negative
 *         errno
 */
ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent)
{
    if (!file || !file->vnode || !file->vnode->fs)
        return -EINVAL;

    if (!S_ISDIR(file->vnode->mode))
        return -ENOTDIR;

    if (!file->vnode->fs->fops.readdir)
        return -ENOSYS;

    return file->vnode->fs->fops.readdir(file, dirent);
}

/**
 * Closes an open file, when its last reference goes: its lock is
 * released, then its socket, device or filesystem closes it.
 *
 * @param file  the open file
 * @return 0, or a negative errno
 */
ssize_t vfs_file_close(struct file *file)
{
    if (!file || !file->vnode)
        return -EINVAL;

    vfs_file_lock_release(file);

    if (file->flags & FILE_SOCKET)
        return socket_shutdown(file, SHUT_RDWR);

    if (ISDEV(file->vnode))
        return kdev_file_close(&VNODE_DEV(file->vnode), file);

    if (!file->vnode->fs)
        return -EINVAL;

    /* by default, the vnode's reference is dropped */
    if (!file->vnode->fs->fops.close)
        return posix_file_close(file);

    return file->vnode->fs->fops.close(file);
}

/**
 * Changes the length of an open file.
 *
 * @param file  the open file
 * @param len   the new length, in bytes
 * @return 0, `-EINVAL` for a socket or a device, or a negative errno
 */
int vfs_file_trunc(struct file *file, off_t len)
{
    if (file && file->flags & FILE_SOCKET)
        return -EINVAL;

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return -EINVAL;

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.trunc)
        return -ENOSYS;

    return file->vnode->fs->fops.trunc(file, len);
}

/**
 * Finds the wait queue that a reader (or a writer) of an open file sleeps
 * on until it can go on, which `poll()` and `select()` wait on too. A
 * socket says which; a device's and a pipe's are their vnode's (see
 * `fs/posix/read.c` and `write.c`).
 *
 * @param file   the open file
 * @param write  whether it's the writers' queue, rather than the readers'
 * @return the wait queue, or NULL if there's none to tell
 */
struct waitq *vfs_file_wait_queue(struct file *file, int write)
{
    if (file->flags & FILE_SOCKET)
        return socket_wait_queue(file, write);

    if (!file->vnode)
        return NULL;

    return write ? file->vnode->write_queue : file->vnode->read_queue;
}

/**
 * Says whether a read of an open file would go on without waiting, for
 * `poll()` and `select()`. A regular file or a directory never waits.
 *
 * @param file  the open file
 * @param size  how many bytes would be read
 * @return 1 if it would, 0 if it would wait, or a negative errno
 */
int vfs_file_can_read(struct file *file, size_t size)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_can_read(file, size);

    if (!file || !file->vnode)
        return -EINVAL;

    /* reading a regular file or a directory never waits */
    if (S_ISREG(file->vnode->mode) || S_ISDIR(file->vnode->mode))
        return 1;

    if (ISDEV(file->vnode))
        return kdev_file_can_read(&VNODE_DEV(file->vnode), file, size);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.can_read)
        return -ENOSYS;

    return file->vnode->fs->fops.can_read(file, size);
}

/**
 * Says whether a write to an open file would go on without waiting, for
 * `poll()`, `select()` and non-blocking writes. A regular file never
 * waits.
 *
 * @param file  the open file
 * @param size  how many bytes would be written
 * @return 1 if it would, 0 if it would wait, or a negative errno
 */
int vfs_file_can_write(struct file *file, size_t size)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_can_write(file, size);

    if (!file || !file->vnode)
        return -EINVAL;

    if (S_ISREG(file->vnode->mode))
        return 1;

    if (ISDEV(file->vnode))
        return kdev_file_can_write(&VNODE_DEV(file->vnode), file, size);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.can_write)
        return -ENOSYS;

    return file->vnode->fs->fops.can_write(file, size);
}

/**
 * Says whether an open file is at its end, so that a read returns 0
 * rather than waiting.
 *
 * @param file  the open file
 * @return 1 at the end, 0 otherwise, or a negative errno
 */
int vfs_file_eof(struct file *file)
{
    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_eof(&VNODE_DEV(file->vnode), file);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.eof)
        return -ENOSYS;

    return file->vnode->fs->fops.eof(file);
}

