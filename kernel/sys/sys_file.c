/** \file
 * System calls: files: opening, reading and writing them, their names
 * and attributes, directories, mounts, locks
 */

#include "syscalls.h"

/**
 * uio to resolve `path' from the directory `dirfd' refers to, AT_FDCWD
 * for the working directory (absolute paths ignore dirfd)
 */
static int at_uio(int dirfd, const char *path, struct uio *uio)
{
    *uio = PROC_UIO(curproc);

    if (dirfd == AT_FDCWD || *path == '/')
        return 0;

    struct file *file = fd_file(dirfd);

    if (!file)
        return -EBADF;

    /* the directory itself (the call holds the file, its vnode stays) */
    if ((file->flags & FILE_SOCKET) || !S_ISDIR(file->vnode->mode))
        return -ENOTDIR;

    uio->cwd = file->vnode;
    return 0;
}

/** user space's `upath' (a kernel copy, *path) and the uio to resolve it
 * from directory `dirfd', for calls taking two paths */
static int at_path(int dirfd, const char *upath, char **path, struct uio *uio)
{
    int ret;

    if ((ret = upath_get(upath, path)))
        return ret;

    if ((ret = at_uio(dirfd, *path, uio))) {
        kfree(*path);
        *path = NULL;
    }

    return ret;
}

/** look up user space's `upath' from `dirfd', AT_SYMLINK_NOFOLLOW in
 * flags: don't follow a final link */
static int at_lookup(int dirfd, const char *upath, int flags, struct uio *uio, struct vnode **vnode)
{
    char *path;
    int ret;

    if ((ret = upath_get(upath, &path)))
        return ret;

    if (!(ret = at_uio(dirfd, path, uio))) {
        if (flags & AT_SYMLINK_NOFOLLOW)
            uio->flags |= O_NOFOLLOW;

        ret = vfs_lookup(path, uio, vnode);
    }

    kfree(path);
    return ret;
}

long sys_close(int fildes)
{
    syscall_log(LOG_DEBUG, "close(fildes=%d)\n", fildes);

    /* the descriptor goes; the open file once nothing else refers to it
     * (another descriptor, a call using it) */
    return proc_fd_close(curproc, fildes);
}

long sys_fstat(int fildes, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "fstat(fildes=%d, buf=%p)\n", fildes, buf);

    struct file *file = fd_file(fildes);

    if (!file) {
        return -EBADF;
    }

    struct aquila_stat st;
    memset(&st, 0, sizeof(st));
    int ret = 0;

    /* sockets have no vnode, report a minimal stat */
    if (file->flags & FILE_SOCKET)
        st.st_mode = S_IFSOCK;
    else
        ret = vfs_stat(file->vnode, &st);

    if (!ret)
        ret = copyout(buf, &st, sizeof(st));

    return ret;
}

long sys_isatty(int fildes)
{
    syscall_log(LOG_DEBUG, "isatty(fildes=%d)\n", fildes);

    struct file *file = fd_file(fildes);

    if (!file) {    /* Invalid File Descriptor */
        return -EBADF;
    }

    /* only character devices can be terminals (libc's isatty() asks the
     * device with TCGETS instead) */
    if ((file->flags & FILE_SOCKET) || !S_ISCHR(file->vnode->mode)) {
        return -ENOTTY;
    }

    return 1;
}

static int do_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    char *opath = NULL, *npath = NULL;
    struct uio ouio, nuio;
    int ret;

    if (flags & ~AT_SYMLINK_FOLLOW)
        return -EINVAL;

    if (!(ret = at_path(olddirfd, oldpath, &opath, &ouio)) &&
        !(ret = at_path(newdirfd, newpath, &npath, &nuio))) {
        /* a link to a symbolic link, not to what it points to */
        if (!(flags & AT_SYMLINK_FOLLOW))
            ouio.flags = O_NOFOLLOW;

        ret = vfs_link(opath, &ouio, npath, &nuio);
    }

    kfree(opath);
    kfree(npath);
    return ret;
}

long sys_link(const char *oldpath, const char *newpath)
{
    syscall_log(LOG_DEBUG, "link(oldpath=%p, newpath=%p)\n", oldpath, newpath);
    return do_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

long sys_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    syscall_log(LOG_DEBUG, "linkat(olddirfd=%d, oldpath=%p, newdirfd=%d, newpath=%p, flags=%x)\n",
            olddirfd, oldpath, newdirfd, newpath, flags);
    return do_linkat(olddirfd, oldpath, newdirfd, newpath, flags);
}

long sys_lseek(int fildes, off_t offset, int whence)
{
    syscall_log(LOG_DEBUG, "lseek(fildes=%d, offset=%ld, whence=%d)\n",
            fildes, (long) offset, whence);

    struct file *file = fd_file(fildes);

    if (!file) {    /* Invalid File Descriptor */
        return -EBADF;
    }

    if (file->flags & FILE_SOCKET) {
        return -ESPIPE;
    }

    /* the new offset, all of it: an int would cut one past 2 GiB */
    off_t ret = vfs_file_lseek(file, offset, whence);
    return ret;
}

/**
 * What O_CREAT creates for `path': the file a dangling symbolic link
 * points to, through links to links. *ref is NULL for `path' itself.
 */
static int creat_path(const char *path, struct uio *uio, char **ref)
{
    struct uio nofollow = *uio;
    nofollow.flags |= O_NOFOLLOW;

    char *cur = NULL;
    *ref = NULL;

    for (int depth = 0; depth < 8; ++depth) {
        const char *p = cur ? cur : path;
        struct vnode *vnode = NULL;

        if (vfs_lookup(p, &nofollow, &vnode)) {
            *ref = cur;
            return 0;
        }

        int link = S_ISLNK(vnode->mode);
        vfs_close(vnode);

        if (!link) {
            kfree(cur);
            return -EEXIST;
        }

        char target[256];
        ssize_t len = vfs_readlink(p, &nofollow, target, sizeof(target) - 1);

        if (len < 0) {
            kfree(cur);
            return len;
        }

        target[len] = '\0';

        /* a relative target is relative to the link's directory */
        size_t dirlen = 0;

        if (target[0] != '/')
            for (size_t i = 0; p[i]; ++i)
                if (p[i] == '/')
                    dirlen = i + 1;

        char *next = kmalloc(dirlen + len + 1, &M_BUFFER, 0);

        if (!next) {
            kfree(cur);
            return -ENOMEM;
        }

        memcpy(next, p, dirlen);
        memcpy(next + dirlen, target, len + 1);

        kfree(cur);
        cur = next;
    }

    kfree(cur);
    return -ELOOP;
}

static int do_openat(int dirfd, const char *upath, int oflags, mode_t mode)
{
    struct uio uio;
    char *path;
    int ret;

    if ((ret = upath_get(upath, &path)))
        return ret;

    if ((ret = at_uio(dirfd, path, &uio))) {
        kfree(path);
        return ret;
    }

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1) { /* Reached maximum number of open file descriptors */
        kfree(path);
        return -EMFILE;
    }

    struct vnode *vnode = NULL;
    struct file *file = NULL;
    int excl = (oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL);
    int created = 0;

    uio.flags = oflags;

    /* O_EXCL never creates through a symbolic link */
    if (excl)
        uio.flags |= O_NOFOLLOW;

    ret = vfs_lookup(path, &uio, &vnode);

    if (!ret && excl) {
        ret = -EEXIST;
        goto done;
    }

    if (ret == -ENOENT && (oflags & O_CREAT)) {
        /* through a dangling symbolic link, what it points to */
        char *target = NULL;

        if (!excl && (ret = creat_path(path, &uio, &target)))
            goto done;

        ret = vfs_creat(target ? target : path, mode & 07777, &uio, &vnode);
        kfree(target);

        if (ret)
            goto done;

        created = 1;
    }

    if (ret)
        goto done;

    /* with O_NOFOLLOW the lookup stops at a final symbolic link */
    if (S_ISLNK(vnode->mode)) {
        ret = -ELOOP;
        goto done;
    }

    if ((oflags & O_DIRECTORY) && !S_ISDIR(vnode->mode)) {
        ret = -ENOTDIR;
        goto done;
    }

    /* the open file: until it's open, not closed if this fails */
    if (!(file = file_new())) {
        ret = -ENOMEM;
        goto done;
    }

    file->vnode = vnode;
    file->flags = oflags & ~O_CLOEXEC;

    /* the creator may open the file whatever mode it was given */
    if (!created && (ret = vfs_perms_check(file, &uio)))
        goto done;

    if ((ret = vfs_file_open(file)))
        goto done;

    proc_fd_install(curproc, fd, file, (oflags & O_CLOEXEC) ? FD_CLOEXEC : 0);
    ret = fd;

done:
    if (ret < 0) {
        proc_fd_release(curproc, fd);
        kfree(file);

        if (vnode)
            vfs_close(vnode);
    }

    kfree(path);
    return ret;
}

long sys_open(const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "open(path=%p, oflags=0x%x, mode=0x%x)\n", path, oflags, mode);
    return do_openat(AT_FDCWD, path, oflags, mode);
}

long sys_openat(int dirfd, const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "openat(dirfd=%d, path=%p, oflags=0x%x, mode=0x%x)\n", dirfd, path, oflags, mode);
    return do_openat(dirfd, path, oflags, mode);
}

long sys_read(int fildes, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "read(fd=%d, buf=%p, count=%d)\n", fildes, buf, nbytes);

    struct file *file = fd_file(fildes);

    if (!file) {
        return -EBADF;
    }

    if (uaccess_check(buf, nbytes)) {
        return -EFAULT;
    }

    struct iobuf io;
    iobuf_init(&io, buf, nbytes, 1);

    ssize_t ret = vfs_file_read(file, &io);
    return ret;
}

static int do_fstatat(int dirfd, const char *path, struct aquila_stat *buf, int flags)
{
    struct vnode *vnode = NULL;
    struct uio uio;
    int ret;

    if (flags & ~AT_SYMLINK_NOFOLLOW)
        return -EINVAL;

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        return ret;

    struct aquila_stat st;
    memset(&st, 0, sizeof(st));

    ret = vfs_stat(vnode, &st);
    vfs_close(vnode);
    return ret ? ret : copyout(buf, &st, sizeof(st));
}

long sys_stat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "stat(path=%p, buf=%p)\n", path, buf);
    return do_fstatat(AT_FDCWD, path, buf, 0);
}

long sys_fstatat(int dirfd, const char *path, struct aquila_stat *buf, int flags)
{
    syscall_log(LOG_DEBUG, "fstatat(dirfd=%d, path=%p, buf=%p, flags=%x)\n", dirfd, path, buf, flags);
    return do_fstatat(dirfd, path, buf, flags);
}

static int do_unlinkat(int dirfd, const char *upath, int flags)
{
    struct uio uio;
    char *path;
    int ret;

    if (flags & ~AT_REMOVEDIR)
        return -EINVAL;

    if ((ret = upath_get(upath, &path)))
        return ret;

    if (!(ret = at_uio(dirfd, path, &uio)))
        ret = (flags & AT_REMOVEDIR) ? vfs_rmdir(path, &uio) : vfs_unlink(path, &uio);

    kfree(path);
    return ret;
}

long sys_unlink(const char *path)
{
    syscall_log(LOG_DEBUG, "unlink(path=%p)\n", path);
    return do_unlinkat(AT_FDCWD, path, 0);
}

long sys_unlinkat(int dirfd, const char *path, int flags)
{
    syscall_log(LOG_DEBUG, "unlinkat(dirfd=%d, path=%p, flags=%x)\n", dirfd, path, flags);
    return do_unlinkat(dirfd, path, flags);
}

long sys_write(int fd, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "write(fd=%d, buf=%p, nbytes=%d)\n", fd, buf, nbytes);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    if (uaccess_check(buf, nbytes)) {
        return -EFAULT;
    }

    struct iobuf io;
    iobuf_init(&io, buf, nbytes, 1);

    ssize_t ret = vfs_file_write(file, &io);

    /* writing to a pipe no one reads: SIGPIPE, the write fails with
     * EPIPE if it is caught or ignored (delivered on the way back to
     * user mode, once the result is the call's) */
    if (ret == -EPIPE)
        signal_proc_send(curproc, SIGPIPE);

    return ret;
}

long sys_ioctl(int fd, int request, void *argp)
{
    syscall_log(LOG_DEBUG, "ioctl(fd=%d, request=0x%x, argp=%p)\n",
            fd, request, argp);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    /* the descriptor's own, whatever it is: non-blocking (as F_SETFL) */
    if (request == FIONBIO) {
        int on;

        if (copyin(&on, argp, sizeof(on))) {
            return -EFAULT;
        }

        if (on)
            file->flags |= O_NONBLOCK;
        else
            file->flags &= ~O_NONBLOCK;

        return 0;
    }

    /* argp is user space's: the socket, the device copy what each
     * request reads and writes */
    if (file->flags & FILE_SOCKET) {
        return socket_ioctl(file, request, argp);
    }

    int ret = vfs_file_ioctl(file, request, argp);
    return ret;
}

long sys_readdir(int fd, struct dirent *dirent)
{
    syscall_log(LOG_DEBUG, "readdir(fd=%d, dirent=%p)\n", fd, dirent);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    if (file->flags & FILE_SOCKET) {
        return -ENOTDIR;
    }

    struct dirent d;
    memset(&d, 0, sizeof(d));
    int ret = vfs_file_readdir(file, &d);

    /* filesystems don't report entry types yet */
    if (ret > 0) {
        d.d_type = DT_UNKNOWN;

        if (copyout(dirent, &d, sizeof(d)))
            ret = -EFAULT;
    }

    return ret;
}

long sys_mount(struct aquila_mount_args *args)
{
    if (curproc->uid != 0) {
        return -EACCES;
    }

    struct aquila_mount_args a;

    if (copyin(&a, args, sizeof(a))) {
        return -EFAULT;
    }

    char *type = NULL, *dir = NULL;

    /* mount data is {char *dev; char *opt;} for all filesystems: the
     * kernel's copy of it, and of its strings */
    char *udata[2], *data[2] = {NULL, NULL};
    int ret;

    if ((ret = upath_get(a.type, &type)) || (ret = upath_get(a.dir, &dir)))
        goto done;

    if (a.data) {
        if (copyin(udata, a.data, sizeof(udata))) {
            ret = -EFAULT;
            goto done;
        }

        for (int i = 0; i < 2; ++i)
            if (udata[i] && (ret = upath_get(udata[i], &data[i])))
                goto done;
    }

    syscall_log(LOG_DEBUG, "mount(type=%s, dir=%s, flags=%x, data=%p)\n",
            type, dir, a.flags, a.data);

    ret = vfs_mount(type, dir, a.flags, a.data ? data : NULL, &PROC_UIO(curproc));

done:
    kfree(type);
    kfree(dir);
    kfree(data[0]);
    kfree(data[1]);
    return ret;
}

static int do_mknodat(int dirfd, const char *upath, mode_t mode, dev_t dev)
{
    struct uio uio;
    char *path;
    int ret;

    /* type 0 is a regular file */
    if (!(mode & S_IFMT))
        mode |= S_IFREG;

    switch (mode & S_IFMT) {
        case S_IFREG:
        case S_IFDIR:
        case S_IFIFO:
            break;
        case S_IFCHR:
        case S_IFBLK:
            /* device files give access to the device */
            if (curproc->uid != 0)
                return -EPERM;
            break;
        default:
            return -EINVAL;
    }

    if ((ret = upath_get(upath, &path)))
        return ret;

    if (!(ret = at_uio(dirfd, path, &uio)))
        ret = vfs_mknod(path, mode, dev, &uio, NULL);

    kfree(path);
    return ret;
}

long sys_mkdir(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "mkdir(path=%p, mode=%x)\n", path, mode);
    return do_mknodat(AT_FDCWD, path, S_IFDIR | (mode & 07777), 0);
}

long sys_pipe(int fd[2])
{
    syscall_log(LOG_DEBUG, "pipe(fd=%p)\n", fd);

    if (uaccess_check(fd, 2 * sizeof(int))) {
        return -EFAULT;
    }

    int err = 0;
    int fd1 = -1, fd2 = -1;
    int fds[2];
    struct file *r = NULL, *w = NULL;

    if ((fd1 = proc_fd_get(curproc)) == -1 || (fd2 = proc_fd_get(curproc)) == -1) {
        err = -EMFILE;
        goto error;
    }

    if (!(r = file_new()) || !(w = file_new())) {
        err = -ENOMEM;
        goto error;
    }

    if ((err = pipefs_pipe(r, w)))
        goto error;

    proc_fd_install(curproc, fd1, r, 0);
    proc_fd_install(curproc, fd2, w, 0);

    fds[0] = fd1;
    fds[1] = fd2;

    /* nowhere to say: the descriptors go again */
    if (copyout(fd, fds, sizeof(fds))) {
        proc_fd_close(curproc, fd1);
        proc_fd_close(curproc, fd2);
        return -EFAULT;
    }

    return 0;

error:
    /* not open: nothing to close */
    kfree(r);
    kfree(w);

    if (fd1 != -1)
        proc_fd_release(curproc, fd1);

    if (fd2 != -1)
        proc_fd_release(curproc, fd2);

    return err;
}

long sys_fcntl(int fd, int cmd, uintptr_t arg)
{
    syscall_log(LOG_DEBUG, "fcntl(fd=%d, cmd=%d, arg=0x%x)\n",
            fd, cmd, arg);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    /* file status flags that F_SETFL may change */
    const int setfl = O_APPEND | O_NONBLOCK;
    int dupfd = 0;

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            if (arg >= FDS_COUNT) {
                return -EINVAL;
            }

            /* lowest numbered free descriptor greater than or equal to arg */
            for (dupfd = arg; dupfd < FDS_COUNT; ++dupfd) {
                if (!curproc->fds[dupfd].file)
                    break;
            }

            if (dupfd == FDS_COUNT) {
                return -EMFILE;
            }

            /* the same open file: its offset and status flags shared */
            proc_fd_install(curproc, dupfd, file_ref(file), cmd == F_DUPFD_CLOEXEC ? FD_CLOEXEC : 0);

            return dupfd;
        case F_GETFD:
            return curproc->fds[fd].flags;
        case F_SETFD:
            /* the descriptor's own */
            curproc->fds[fd].flags = arg & FD_CLOEXEC;
            return 0;
        case F_GETFL:
            return file->flags & ~FILE_SOCKET;
        case F_SETFL:
            /* the open file's: every descriptor referring to it */
            file->flags = (file->flags & ~setfl) | ((int) arg & setfl);
            return 0;
    }

    return -EINVAL;
}

/** the current directory is `vnode' (a searchable directory; the
 * reference is the process's now) */
static int cwd_set(struct vnode *vnode)
{
    int ret;

    if (!S_ISDIR(vnode->mode))
        return -ENOTDIR;

    if ((ret = vfs_access(vnode, &PROC_UIO(curproc), VFS_X)))
        return ret;

    if (curproc->cwd)
        vfs_close(curproc->cwd);

    curproc->cwd = vnode;
    return 0;
}

long sys_chdir(const char *upath)
{
    syscall_log(LOG_DEBUG, "chdir(path=%p)\n", upath);

    char *path = NULL;
    struct vnode *vnode = NULL;
    int ret = upath_get(upath, &path);

    if (!ret)
        ret = vfs_lookup(path, &PROC_UIO(curproc), &vnode);

    if (!ret && !(ret = cwd_set(vnode)))
        vnode = NULL;

    if (vnode)
        vfs_close(vnode);

    kfree(path);
    return ret;
}

long sys_getcwd(char *buf, size_t size)
{
    syscall_log(LOG_DEBUG, "getcwd(buf=%p, size=%d)\n", buf, size);

    if (!size) {
        return -EINVAL;
    }

    /* made from the directory up (it isn't kept as a string) */
    size = MIN(size, UPATH_MAX);
    char *path = kmalloc(size, &M_BUFFER, 0);
    int ret;

    if (!path) {
        return -ENOMEM;
    }

    if (!curproc->cwd)
        ret = size < 2 ? -ERANGE : (strcpy(path, "/"), 0);
    else
        ret = vfs_vnode_path(curproc->cwd, NULL, path, size);

    if (!ret)
        ret = copyout(buf, path, strlen(path) + 1);

    kfree(path);
    return ret;
}

long sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknod(path=%s, mode=%x, dev=%x)\n", path, mode, dev);
    return do_mknodat(AT_FDCWD, path, mode, dev);
}

long sys_mknodat(int dirfd, const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknodat(dirfd=%d, path=%s, mode=%x, dev=%x)\n", dirfd, path, mode, dev);
    return do_mknodat(dirfd, path, mode, dev);
}

long sys_lstat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "lstat(path=%s, buf=%p)\n", path, buf);
    return do_fstatat(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}

long sys_ftruncate(int fildes, off_t len)
{
    syscall_log(LOG_DEBUG, "ftruncate(fildes=%d, len=%d)\n", fildes, len);

    struct file *file = fd_file(fildes);

    if (!file) {
        return -EBADF;
    }

    /* must be open for writing */
    if (len < 0 || (file->flags & O_ACCMODE) == O_RDONLY) {
        return -EINVAL;
    }

    return vfs_file_trunc(file, len);
}

long sys_umask(mode_t mask)
{
    syscall_log(LOG_DEBUG, "umask(mask=%d)\n", mask);

    mode_t cur_mask = curproc->mask;
    curproc->mask = mask & 0777;

    return cur_mask;
}

static int do_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    struct vnode *vnode = NULL;
    struct uio uio;
    int ret;

    /* symbolic links have no mode of their own */
    if (flags & AT_SYMLINK_NOFOLLOW)
        return -EOPNOTSUPP;

    if (flags)
        return -EINVAL;

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        return ret;

    ret = vfs_chmod(vnode, mode, &uio);
    vfs_close(vnode);
    return ret;
}

long sys_chmod(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "chmod(path=%p, mode=%o)\n", path, mode);
    return do_fchmodat(AT_FDCWD, path, mode, 0);
}

long sys_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    syscall_log(LOG_DEBUG, "fchmodat(dirfd=%d, path=%p, mode=%o, flags=%x)\n", dirfd, path, mode, flags);
    return do_fchmodat(dirfd, path, mode, flags);
}

/** the vnode of an open file, NULL for sockets */
static struct vnode *fd_vnode(int fd, int *err)
{
    struct file *file = fd_file(fd);

    if (!file || (file->flags & FILE_SOCKET)) {
        *err = file ? -EINVAL : -EBADF;
        return NULL;
    }

    return file->vnode;
}

long sys_fchmod(int fd, mode_t mode)
{
    syscall_log(LOG_DEBUG, "fchmod(fd=%d, mode=%o)\n", fd, mode);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chmod(vnode, mode, &PROC_UIO(curproc));

    return ret;
}

#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4

static int do_faccessat(int dirfd, const char *path, int mode, int flags)
{
    struct vnode *vnode = NULL;
    struct uio uio;
    int ret;

    /* there are no separate effective ids, AT_EACCESS changes nothing */
    if ((flags & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) || (mode & ~(R_OK | W_OK | X_OK)))
        return -EINVAL;

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        return ret;

    int vmode = (mode & R_OK ? VFS_R : 0) | (mode & W_OK ? VFS_W : 0) | (mode & X_OK ? VFS_X : 0);

    ret = vfs_access(vnode, &uio, vmode);
    vfs_close(vnode);
    return ret;
}

long sys_access(const char *path, int mode)
{
    syscall_log(LOG_DEBUG, "access(path=%p, mode=%d)\n", path, mode);
    return do_faccessat(AT_FDCWD, path, mode, 0);
}

long sys_faccessat(int dirfd, const char *path, int mode, int flags)
{
    syscall_log(LOG_DEBUG, "faccessat(dirfd=%d, path=%p, mode=%d, flags=%x)\n", dirfd, path, mode, flags);
    return do_faccessat(dirfd, path, mode, flags);
}

static int do_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    struct vnode *vnode = NULL;
    struct uio uio;
    int ret;

    if (flags & ~AT_SYMLINK_NOFOLLOW)
        return -EINVAL;

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        return ret;

    ret = vfs_chown(vnode, owner, group, &uio);
    vfs_close(vnode);
    return ret;
}

long sys_chown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "chown(path=%p, owner=%d, group=%d)\n", path, owner, group);
    return do_fchownat(AT_FDCWD, path, owner, group, 0);
}

long sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    syscall_log(LOG_DEBUG, "fchownat(dirfd=%d, path=%p, owner=%d, group=%d, flags=%x)\n",
            dirfd, path, owner, group, flags);
    return do_fchownat(dirfd, path, owner, group, flags);
}

long sys_fchown(int fd, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "fchown(fd=%d, owner=%d, group=%d)\n", fd, owner, group);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chown(vnode, owner, group, &PROC_UIO(curproc));

    return ret;
}

long sys_lchown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "lchown(path=%p, owner=%d, group=%d)\n", path, owner, group);
    return do_fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}

/** `path' NULL: the file `dirfd' refers to (futimens) */
long sys_utimensat(int dirfd, const char *path, const struct aquila_timespec times[2], int flags)
{
    syscall_log(LOG_DEBUG, "utimensat(dirfd=%d, path=%p, times=%p, flags=%x)\n", dirfd, path, times, flags);

    struct vnode *vnode = NULL;
    struct uio uio = PROC_UIO(curproc);
    int ret = 0;

    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        ret = -EINVAL;
        goto done;
    }

    struct aquila_timespec ktimes[2];

    if (times && copyin(ktimes, times, sizeof(ktimes))) {
        ret = -EFAULT;
        goto done;
    }

    if (!path) {
        if ((vnode = fd_vnode(dirfd, &ret)))
            ret = vfs_utimens(vnode, times ? ktimes : NULL, &uio);

        goto done;
    }

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        goto done;

    ret = vfs_utimens(vnode, times ? ktimes : NULL, &uio);
    vfs_close(vnode);

done:
    return ret;
}

long sys_rmdir(const char *path)
{
    syscall_log(LOG_DEBUG, "rmdir(path=%p)\n", path);
    return do_unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

long sys_symlinkat(const char *utarget, int dirfd, const char *upath)
{
    syscall_log(LOG_DEBUG, "symlinkat(target=%p, dirfd=%d, path=%p)\n", utarget, dirfd, upath);

    char *target = NULL, *path = NULL;
    struct uio uio;
    int ret;

    if (!(ret = upath_get(utarget, &target)) &&
        !(ret = upath_get(upath, &path)) &&
        !(ret = at_uio(dirfd, path, &uio)))
        ret = vfs_symlink(target, path, &uio);

    kfree(target);
    kfree(path);
    return ret;
}

long sys_readlinkat(int dirfd, const char *upath, char *buf, size_t size)
{
    syscall_log(LOG_DEBUG, "readlinkat(dirfd=%d, path=%p, buf=%p, size=%d)\n", dirfd, upath, buf, size);

    /* the target into the kernel's buffer (a path: no longer than one),
     * then out */
    char *path = NULL, *target = NULL;
    struct uio uio;
    ssize_t ret;

    if (uaccess_check(buf, size)) {
        return -EFAULT;
    }

    if (!(ret = upath_get(upath, &path)) && !(ret = at_uio(dirfd, path, &uio))) {
        if (!(target = kmalloc(UPATH_MAX, &M_BUFFER, 0)))
            ret = -ENOMEM;
        else if ((ret = vfs_readlink(path, &uio, target, MIN(size, UPATH_MAX))) > 0 &&
                 copyout(buf, target, ret))
            ret = -EFAULT;
    }

    kfree(path);
    kfree(target);
    return ret;
}

long sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    syscall_log(LOG_DEBUG, "renameat(olddirfd=%d, oldpath=%p, newdirfd=%d, newpath=%p)\n",
            olddirfd, oldpath, newdirfd, newpath);

    char *opath = NULL, *npath = NULL;
    struct uio ouio, nuio;
    int ret;

    if (!(ret = at_path(olddirfd, oldpath, &opath, &ouio)) &&
        !(ret = at_path(newdirfd, newpath, &npath, &nuio)))
        ret = vfs_rename(opath, &ouio, npath, &nuio);

    kfree(opath);
    kfree(npath);
    return ret;
}

long sys_flock(int fd, int op)
{
    syscall_log(LOG_DEBUG, "flock(fd=%d, op=%d)\n", fd, op);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    return vfs_file_flock(file, op);
}

long sys_sync(void)
{
    syscall_log(LOG_DEBUG, "sync()\n");

    /* writes go to the devices right away, only their caches are left */
    return kdev_sync();
}

long sys_fchdir(int fd)
{
    syscall_log(LOG_DEBUG, "fchdir(fd=%d)\n", fd);

    struct file *file = fd_file(fd);
    int ret;

    if (!file) {
        ret = -EBADF;
    } else if (file->flags & FILE_SOCKET) {
        ret = -ENOTDIR;
    } else {
        /* the directory the descriptor has, however it was reached */
        file->vnode->ref++;

        if ((ret = cwd_set(file->vnode)))
            vfs_close(file->vnode);
    }

    return ret;
}
