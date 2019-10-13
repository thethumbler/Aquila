#include <core/system.h>
#include <fs/vfs.h>
#include <dev/dev.h>
#include <net/socket.h>
#include <bits/fcntl.h>

/**
 * \ingroup vfs
 * \brief open a new file
 */
int vfs_file_open(struct file *file)
{
    if (!file || !file->vnode || !file->vnode->fs)
        return -EINVAL;

    if (S_ISDIR(file->vnode->mode) && !(file->flags & O_SEARCH))
        return -EISDIR;

    if (ISDEV(file->vnode))
        return kdev_file_open(&VNODE_DEV(file->vnode), file);

    if (!file->vnode->fs->fops.open)
        return -ENOSYS;

    return file->vnode->fs->fops.open(file);
}

/**
 * \ingroup vfs
 * \brief read from an open file
 */
ssize_t vfs_file_read(struct file *file, void *buf, size_t nbytes)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_recv(file, buf, nbytes, 0);

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_read(&VNODE_DEV(file->vnode), file, buf, nbytes);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.read)
        return -ENOSYS;

    return file->vnode->fs->fops.read(file, buf, nbytes);
}

/**
 * \ingroup vfs
 * \brief write to an open file
 */
ssize_t vfs_file_write(struct file *file, void *buf, size_t nbytes)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_send(file, buf, nbytes, 0);

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_write(&VNODE_DEV(file->vnode), file, buf, nbytes);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.write)
        return -ENOSYS;

    return file->vnode->fs->fops.write(file, buf, nbytes);
}

/**
 * \ingroup vfs
 * \brief perform ioctl on an open file
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
 * \ingroup vfs
 * \brief perform a seek in an open file
 */
off_t vfs_file_lseek(struct file *file, off_t offset, int whence)
{
    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_lseek(&VNODE_DEV(file->vnode), file, offset, whence);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.lseek)
        return -ENOSYS;

    return file->vnode->fs->fops.lseek(file, offset, whence);
}

/**
 * \ingroup vfs
 * \brief read entries from an open directory
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
 * \ingroup vfs
 * \brief close an open file
 */
ssize_t vfs_file_close(struct file *file)
{
    if (!file || !file->vnode)
        return -EINVAL;

    if (file->flags & FILE_SOCKET)
        return socket_shutdown(file, SHUT_RDWR);

    if (ISDEV(file->vnode))
        return kdev_file_close(&VNODE_DEV(file->vnode), file);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.close)
        return -ENOSYS;

    return file->vnode->fs->fops.close(file);
}

/**
 * \ingroup vfs
 * \brief truncate an open file
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

int vfs_file_can_read(struct file *file, size_t size)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_can_read(file, size);

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_can_read(&VNODE_DEV(file->vnode), file, size);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.can_read)
        return -ENOSYS;

    return file->vnode->fs->fops.can_read(file, size);
}

int vfs_file_can_write(struct file *file, size_t size)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_can_write(file, size);

    if (!file || !file->vnode)
        return -EINVAL;

    if (ISDEV(file->vnode))
        return kdev_file_can_write(&VNODE_DEV(file->vnode), file, size);

    if (!file->vnode->fs)
        return -EINVAL;

    if (!file->vnode->fs->fops.can_write)
        return -ENOSYS;

    return file->vnode->fs->fops.can_write(file, size);
}

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

