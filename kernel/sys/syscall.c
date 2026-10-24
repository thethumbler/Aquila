/**********************************************************************
 *                          System Calls
 */


#include <core/system.h>
#include <aquila/syscall.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/arch.h>
#include <core/time.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>
#include <sys/binfmt.h>
#include <sys/uaccess.h>
#include <sys/syscall.h>

#include <fs/iobuf.h>

#include <net/socket.h>

#include <aquila/errno.h>
#include <aquila/dirent.h>
#include <aquila/utsname.h>
#include <aquila/fcntl.h>
#include <aquila/mman.h>
#include <aquila/time.h>
#include <aquila/ioctl.h>
#include <aquila/wait.h>
#include <aquila/poll.h>
#include <aquila/un.h>

#include <fs/devpts.h>
#include <fs/pipe.h>
#include <fs/stat.h>

#include <mm/vm.h>
#include <sys/reboot.h>
#include <dev/random.h>

static int syscall_log_level = LOG_NONE;
LOGGER_DEFINE(syscall, syscall_log, syscall_log_level)

/** the open file `fd' refers to, held until the call returns (a close
 * meanwhile doesn't take it away); NULL if `fd' is not open */
static struct file *fd_file(int fd)
{
    struct file *file = proc_fd_file(curproc, fd);
    return file ? file_hold(file) : NULL;
}

/** the same, not held: only until the call sleeps (select, poll) */
static struct file *fd_peek(int fd)
{
    return proc_fd_file(curproc, fd);
}

/*
 * User memory: what a call reads from it comes in with copyin() and
 * copyinstr() (a path: upath_get()), what it writes goes out with
 * copyout(), data through an iobuf (fs/iobuf.h); a bad address is EFAULT.
 * Below them the kernel works with its own copies only.
 */

/* the longest path (with its NUL) a call takes, as newlib's PATH_MAX */
#define UPATH_MAX   1024

/** user space's path into a kernel copy, *path (the caller kfree()s it) */
static int upath_get(const char *upath, char **path)
{
    char *buf = kmalloc(UPATH_MAX, &M_BUFFER, 0);

    *path = NULL;

    if (!buf)
        return -ENOMEM;

    long len = copyinstr(buf, upath, UPATH_MAX);

    if (len < 0) {
        kfree(buf);
        return len;
    }

    *path = buf;
    return 0;
}

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

static long sys_exit(int code)
{
    syscall_log(LOG_DEBUG, "exit(code=%d)\n", code);

    struct proc *owner = curproc;

    owner->exit = PROC_EXIT(code, 0);  /* Child exited normally */

    proc_kill(owner);
    arch_sleep();

    /* not reached: the process is gone, this thread with it */
    for (;;);
    return 0;
}

static long sys_close(int fildes)
{
    syscall_log(LOG_DEBUG, "close(fildes=%d)\n", fildes);

    /* the descriptor goes; the open file once nothing else refers to it
     * (another descriptor, a call using it) */
    return proc_fd_close(curproc, fildes);
}

static long sys_execve(const char *path, char * const argp[], char * const envp[])
{
    syscall_log(LOG_DEBUG, "execve(path=%p, argp=%p, envp=%p)\n", path, argp, envp);

    char *fn;
    int err = upath_get(path, &fn);

    if (err) {
        return err;
    }

    if (!*fn) {
        kfree(fn);
        return -ENOENT;
    }

    err = proc_execve(curthread, fn, argp, envp);

    kfree(fn);

    if (err)
        return err;

    /* the new program, from its entry point: no return from here */
    sched_thread_spawn(curthread);
    return 0;
}

static long sys_fork(void)
{
    syscall_log(LOG_DEBUG, "fork()\n");

    struct proc *fork = NULL;
    int err = proc_fork(curthread, &fork);

    if (err) {
        return err;
    }

    /* the child runs, from fork()'s return (0: proc_fork()) */
    struct thread *thread = (struct thread *) fork->threads.head->value;
    sched_thread_ready(thread);

    return fork->pid;
}

static long sys_fstat(int fildes, struct aquila_stat *buf)
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

static long sys_getpid(void)
{
    syscall_log(LOG_DEBUG, "getpid()\n");
    return curproc->pid;
}

static long sys_isatty(int fildes)
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

static long sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);

    /* one sent to ourselves is delivered before this returns to user
     * mode (x86_return_to_user) */
    return signal_send(pid, sig);
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

static long sys_link(const char *oldpath, const char *newpath)
{
    syscall_log(LOG_DEBUG, "link(oldpath=%p, newpath=%p)\n", oldpath, newpath);
    return do_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

static long sys_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    syscall_log(LOG_DEBUG, "linkat(olddirfd=%d, oldpath=%p, newdirfd=%d, newpath=%p, flags=%x)\n",
            olddirfd, oldpath, newdirfd, newpath, flags);
    return do_linkat(olddirfd, oldpath, newdirfd, newpath, flags);
}

static long sys_lseek(int fildes, off_t offset, int whence)
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

static long sys_open(const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "open(path=%p, oflags=0x%x, mode=0x%x)\n", path, oflags, mode);
    return do_openat(AT_FDCWD, path, oflags, mode);
}

static long sys_openat(int dirfd, const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "openat(dirfd=%d, path=%p, oflags=0x%x, mode=0x%x)\n", dirfd, path, oflags, mode);
    return do_openat(dirfd, path, oflags, mode);
}

static long sys_read(int fildes, void *buf, size_t nbytes)
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

static long sys_sbrk(ptrdiff_t incr)
{
    syscall_log(LOG_DEBUG, "sbrk(incr=0x%x)\n", incr);

    uintptr_t heap_start = curproc->heap_start;
    uintptr_t heap = curproc->heap;

    /* the heap may grow up to the next mapped region (or the stack) */
    uintptr_t heap_limit = USER_STACK_BASE;
    struct qnode *next = curproc->heap_vm->qnode->next;

    if (next)
        heap_limit = ((struct vm_entry *) next->value)->base;

    if ((incr < 0 && (uintptr_t) -incr > heap - heap_start) ||
        (incr > 0 && (heap_limit < heap || (uintptr_t) incr > heap_limit - heap))) {
        /* libc passes the return value through and only treats
         * (void *) -1 as failure, so return that instead of -ENOMEM */
        return (uintptr_t) -1;
    }

    curproc->heap = heap + incr;
    curproc->heap_vm->size = PAGE_ROUND(heap + incr - heap_start);

    return heap;
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

static long sys_stat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "stat(path=%p, buf=%p)\n", path, buf);
    return do_fstatat(AT_FDCWD, path, buf, 0);
}

static long sys_fstatat(int dirfd, const char *path, struct aquila_stat *buf, int flags)
{
    syscall_log(LOG_DEBUG, "fstatat(dirfd=%d, path=%p, buf=%p, flags=%x)\n", dirfd, path, buf, flags);
    return do_fstatat(dirfd, path, buf, flags);
}

static long sys_times(struct aquila_tms *buf)
{
    syscall_log(LOG_DEBUG, "times(buf=%p)\n", buf);

    const uint64_t tick = 1000000000ULL / AQUILA_CLK_TCK;   /* ns */

    struct aquila_tms tms = {
        .tms_utime  = curproc->utime / tick,
        .tms_stime  = curproc->stime / tick,
        .tms_cutime = curproc->cutime / tick,
        .tms_cstime = curproc->cstime / tick,
    };

    if (copyout(buf, &tms, sizeof(tms))) {
        return -EFAULT;
    }

    /* elapsed real time, since boot */
    return (uint32_t) (arch_rtime_ns() / tick);
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

static long sys_unlink(const char *path)
{
    syscall_log(LOG_DEBUG, "unlink(path=%p)\n", path);
    return do_unlinkat(AT_FDCWD, path, 0);
}

static long sys_unlinkat(int dirfd, const char *path, int flags)
{
    syscall_log(LOG_DEBUG, "unlinkat(dirfd=%d, path=%p, flags=%x)\n", dirfd, path, flags);
    return do_unlinkat(dirfd, path, flags);
}

static long sys_waitpid(int pid, int *stat_loc, int options)
{
    syscall_log(LOG_DEBUG, "waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", pid, stat_loc, options);

    /* pid > 0:   that child
     * pid == -1: any child
     * pid == 0:  any child in the caller's process group
     * pid < -1:  any child in the process group -pid */
    pid_t pgid = pid == 0 ? curproc->pgrp->pgid : -pid;

    for (;;) {
        int found = 0;

        queue_for (node, procs) {
            struct proc *proc = node->value;
            int status;

            if (proc->parent != curproc)
                continue;

            if (pid > 0 ? proc->pid != pid : pid != -1 && proc->pgrp->pgid != pgid)
                continue;

            found = 1;

            if (proc->zombie) {
                status = proc->exit;
            } else if ((options & WUNTRACED) && (proc->wait_event & WAIT_STOPPED)) {
                proc->wait_event &= ~WAIT_STOPPED;
                status = (proc->stopped << 8) | 0x7f;
            } else if ((options & WCONTINUED) && (proc->wait_event & WAIT_CONTINUED)) {
                proc->wait_event &= ~WAIT_CONTINUED;
                status = 0xffff;
            } else {
                continue;
            }

            /* nowhere to put it: the child stays as it is */
            if (stat_loc && copyout(stat_loc, &status, sizeof(status))) {
                return -EFAULT;
            }

            pid_t child = proc->pid;

            if (proc->zombie)
                proc_reap(proc);

            return child;
        }

        if (!found) {
            return -ECHILD;
        }

        if (options & WNOHANG) {
            return 0;
        }

        if (waitq_sleep(&curproc->wait_queue)) {
            return -ERESTARTSYS;
        }
    }
}

static long sys_write(int fd, void *buf, size_t nbytes)
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

static long sys_ioctl(int fd, int request, void *argp)
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

static long sys_sigaction(int sig, const struct aquila_sigaction *act, struct aquila_sigaction *oact)
{
    syscall_log(LOG_DEBUG, "sigaction(sig=%d, act=%p, oact=%p)\n",
            sig, act, oact);

    if (sig < 1 || sig > SIG_MAX) {
        return -EINVAL;
    }

    struct aquila_sigaction new;

    if (act && copyin(&new, act, sizeof(new))) {
        return -EFAULT;
    }

    if (act) {
        uintptr_t handler = new.sa_handler;

        /* SIGKILL and SIGSTOP can not be caught or ignored */
        if ((sig == SIGKILL || sig == SIGSTOP) && handler != SIG_DFL) {
            return -EINVAL;
        }

        /* handlers must live in user space */
        if (handler != SIG_DFL && handler != SIG_IGN && uaccess_check((void *) handler, 1)) {
            return -EFAULT;
        }
    }

    if (oact && copyout(oact, &curproc->sigaction[sig], sizeof(struct aquila_sigaction))) {
        return -EFAULT;
    }

    if (act) {
        curproc->sigaction[sig] = new;
        signal_action_set(curproc, sig);
    }

    return 0;
}

static long sys_readdir(int fd, struct dirent *dirent)
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

static long sys_mount(struct aquila_mount_args *args)
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

static long sys_mkdir(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "mkdir(path=%p, mode=%x)\n", path, mode);
    return do_mknodat(AT_FDCWD, path, S_IFDIR | (mode & 07777), 0);
}

/* set by sethostname(), shown by uname() */
static char nodename[sizeof(((struct utsname *) 0)->nodename)] = UTSNAME_NODENAME;

static long sys_uname(struct utsname *name)
{
    syscall_log(LOG_DEBUG, "uname(name=%p)\n", name);

    struct utsname u;
    memset(&u, 0, sizeof(u));

    strcpy(u.sysname,  UTSNAME_SYSNAME);
    strcpy(u.nodename, nodename);
    strcpy(u.release,  UTSNAME_RELEASE);
    /* the compiler's version string may be long */
    size_t n = MIN(sizeof(u.version) - 1, (size_t) strlen(UTSNAME_VERSION));
    memcpy(u.version, UTSNAME_VERSION, n);
    strcpy(u.machine,  UTSNAME_MACHINE);

    return copyout(name, &u, sizeof(u));
}

static long sys_pipe(int fd[2])
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

static long sys_fcntl(int fd, int cmd, uintptr_t arg)
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

static long sys_chdir(const char *upath)
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

static long sys_getcwd(char *buf, size_t size)
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

static long sys_thread_create(struct aquila_thread_args *__uthread)
{
    struct aquila_thread_args a;

    if (copyin(&a, __uthread, sizeof(a))) {
        return -EFAULT;
    }

    syscall_log(LOG_DEBUG,
            "thread_create(stack=%p, entry=%p, uentry=%p, arg=%p, attr=%p)\n",
            a.stack, a.entry, a.uentry, a.arg, a.attr);

    uintptr_t stack = a.stack;

    /* the kernel pushes the initial frame (3 words) on the user stack */
    if (stack < 3 * sizeof(uintptr_t) ||
        uaccess_check((void *) (stack - 3 * sizeof(uintptr_t)), 3 * sizeof(uintptr_t))) {
        return -EFAULT;
    }

    int err = 0;
    struct thread *thread = NULL;

    if ((err = thread_create(curthread, stack, a.entry, a.uentry, a.arg, a.attr, &thread))) {
        return err;
    }

    sched_thread_ready(thread);
    return thread->tid;
}

static long sys_thread_exit(void *value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_exit(value_ptr=%p)\n", value_ptr);

    curthread->exit_value = value_ptr;
    struct proc *owner = curproc;

    /* last live thread exiting terminates the process */
    int alive = 0;
    queue_for (node, &owner->threads) {
        struct thread *thread = node->value;
        if (thread != curthread && thread->state != ZOMBIE)
            ++alive;
    }

    if (!alive)
        sys_exit(0);

    /* Wakeup owner if it is waiting for joining */
    waitq_wakeup(&owner->thread_join);

    /* The thread stays in owner->threads as a zombie, freed by
     * thread_join, or when the process exits. Its kernel stack (which we
     * are running on) is released from the idle stack by
     * arch_cur_thread_kill */
    curthread->state = ZOMBIE;
    arch_cur_thread_kill();

    /* not reached: the thread is gone */
    for (;;);
    return 0;
}

static long sys_thread_join(int tid, void **value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_join(tid=%d, value_ptr=%p)\n", tid, value_ptr);

    struct proc *owner = curproc;
    struct thread *thread = NULL;

    queue_for (node, &owner->threads) {
        struct thread *_thread = node->value;
        if (_thread->tid == tid)
            thread = _thread;
    }

    /* No such thread (or joined already) */
    if (!thread) {
        return -ESRCH;
    }

    /* it would wait for itself forever */
    if (thread == curthread) {
        return -EDEADLK;
    }

    while (thread->state != ZOMBIE) {
        if (waitq_sleep(&curproc->thread_join)) {
            return -ERESTARTSYS;
        }
    }

    if (value_ptr && copyout(value_ptr, &thread->exit_value, sizeof(void *))) {
        return -EFAULT;
    }

    /* joined: what's left of it goes (thread_exit freed the rest; its
     * CPU is done with it, it let go of the kernel lock since) */
    int joined = thread->tid;
    queue_remove(&owner->threads, thread);
    kfree(thread);

    return joined;
}

static long sys_setpgid(pid_t pid, pid_t pgid)
{
    syscall_log(LOG_DEBUG, "setpgid(pid=%d, pgid=%d)\n", pid, pgid);

    if (pgid < 0) {
        return -EINVAL;
    }

    /* the caller or one of its children */
    struct proc *proc = pid ? proc_pid_find(pid) : curproc;

    if (!proc || (proc != curproc && proc->parent != curproc)) {
        return -ESRCH;
    }

    struct session *session = proc->pgrp->session;

    /* a session leader can't change its group, nor can a process outside
     * the caller's session */
    if (session != curproc->pgrp->session || session->leader == proc) {
        return -EPERM;
    }

    return proc_setpgid(proc, pgid ? pgid : proc->pid);
}

static long sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknod(path=%s, mode=%x, dev=%x)\n", path, mode, dev);
    return do_mknodat(AT_FDCWD, path, mode, dev);
}

static long sys_mknodat(int dirfd, const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknodat(dirfd=%d, path=%s, mode=%x, dev=%x)\n", dirfd, path, mode, dev);
    return do_mknodat(dirfd, path, mode, dev);
}

static long sys_lstat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "lstat(path=%s, buf=%p)\n", path, buf);
    return do_fstatat(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}

/**
 * There are no separate effective or saved ids, setuid() and setgid()
 * set the process ids: root may set any id, other users only their own.
 */
static long sys_setuid(uid_t uid)
{
    syscall_log(LOG_DEBUG, "setuid(uid=%d)\n", uid);

    if (curproc->uid != 0 && uid != curproc->uid) {
        return -EPERM;
    }

    curproc->uid = uid;
    return 0;
}

static long sys_ftruncate(int fildes, off_t len)
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

static long sys_setgid(gid_t gid)
{
    syscall_log(LOG_DEBUG, "setgid(gid=%d)\n", gid);

    if (curproc->uid != 0 && gid != curproc->gid) {
        return -EPERM;
    }

    curproc->gid = gid;
    return 0;
}

static long sys_getuid(void)
{
    syscall_log(LOG_DEBUG, "getuid()\n");
    return curproc->uid;
}

static long sys_getgid(void)
{
    syscall_log(LOG_DEBUG, "getgid()\n");
    return curproc->gid;
}

static long sys_mmap(struct aquila_mmap_args *uargs, void **ret)
{
    struct aquila_mmap_args _args, *args = &_args;
    void *none = NULL;

    /* the address goes out at the end: where to, writable, first */
    if (copyin(args, uargs, sizeof(*args)) || copyout(ret, &none, sizeof(none))) {
        return -EFAULT;
    }

    syscall_log(LOG_DEBUG, "mmap(addr=%p, len=%d, prot=%x, flags=%x, fildes=%d, off=%d, ret=%p)\n",
            args->addr, args->len, args->prot, args->flags, args->fildes, args->off, ret);

    int err = 0;
    int anon = args->flags & MAP_ANONYMOUS;

    /* anonymous memory is the process's own (fork() copies it) */
    if (anon && (args->flags & MAP_SHARED)) {
        return -EINVAL;
    }

    struct file *file = anon ? NULL : fd_file(args->fildes);

    if (!anon && !file) {    /* Invalid File Descriptor */
        return -EBADF;
    }

    if (file && (file->flags & FILE_SOCKET)) {
        return -ENODEV;
    }

    if (!args->len) {
        return -EINVAL;
    }

    /* a fixed mapping must lie entirely in user space, on pages */
    if ((args->flags & MAP_FIXED) && (((uintptr_t) args->addr & PAGE_MASK) ||
                uaccess_check(args->addr, args->len))) {
        return -EINVAL;
    }

    /* Allocate VMR */
    struct vm_entry *vm_entry;
    vm_entry = vm_entry_new();

    if (vm_entry == NULL) {
        err = -ENOMEM;
        goto error;
    }

    /* Initialize VM entry */
    vm_entry->base   = (uintptr_t) args->addr;
    vm_entry->size   = PAGE_ROUND(args->len);
    vm_entry->flags  = args->prot & PROT_READ  ? VM_UR : 0;
    vm_entry->flags |= args->prot & PROT_WRITE ? VM_UW : 0;
    vm_entry->flags |= args->prot & PROT_EXEC  ? VM_UX : 0;
    vm_entry->flags |= args->flags & MAP_SHARED ? VM_SHARED : 0;
    vm_entry->off    = anon ? 0 : args->off;

    /* without an object, pages are zero-filled when first used */
    vm_entry->vm_object = anon ? NULL : vm_object_vnode(file->vnode);

    if (!anon && !vm_entry->vm_object) {
        err = -ENOMEM;
        goto error;
    }

    if (vm_entry->vm_object)
        vm_object_incref(vm_entry->vm_object);

    if (!(args->flags & MAP_FIXED))
        vm_entry->base = 0;  /* Allocate memory region */

    struct vm_space *vm_space = &curproc->vm_space;
    if ((err = vm_space_insert(vm_space, vm_entry)))
        goto error;

    if (!anon && !(args->flags & MAP_PRIVATE) && (err = vfs_map(vm_space, vm_entry)))
        goto error;

    void *addr = (void *) vm_entry->base;
    err = copyout(ret, &addr, sizeof(addr));

    return err;

error:
    if (vm_entry) {
        if (vm_entry->qnode)
            queue_node_remove(&curproc->vm_space.vm_entries, vm_entry->qnode);

        if (vm_entry->vm_object)
            vm_object_decref(vm_entry->vm_object);

        kfree(vm_entry);
    }

    return err;
}

static long sys_munmap(void *addr, size_t len)
{
    syscall_log(LOG_DEBUG, "munmap(addr=%p, len=%d)\n", addr, len);

    struct vm_space *vm_space = &curproc->vm_space;

    if (!len) {
        return -EINVAL;
    }

    queue_for (node, &vm_space->vm_entries) {
        struct vm_entry *vm_entry = node->value;

        /* heap and stack are managed by the kernel (sbrk/exec) */
        if (vm_entry == curproc->heap_vm || vm_entry == curproc->stack_vm)
            continue;

        if (vm_entry->base == (uintptr_t) addr && vm_entry->size == PAGE_ROUND(len)) {
            queue_node_remove(&vm_space->vm_entries, vm_entry->qnode);
            vm_unmap_full(vm_space, vm_entry);
            vm_entry_destroy(vm_entry);
            kfree(vm_entry);
            return 0;
        }
    }

    /* Not found */
    return -EINVAL;
}

static long sys_socket(int domain, int type, int protocol)
{
    syscall_log(LOG_DEBUG, "socket(domain=%d, type=%d, protocol=%d)\n",
            domain, type, protocol);

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        return -EMFILE;
    }
    
    int err = 0;
    struct file *file = file_new();

    if (!file || (err = socket_create(file, domain, type, protocol))) {
        /* not open: nothing to close */
        kfree(file);
        proc_fd_release(curproc, fd);
        return file ? err : -ENOMEM;
    }

    proc_fd_install(curproc, fd, file, 0);
    return fd;
}

/**
 * Socket addresses: the kernel's copy, as big as the biggest (AF_UNIX's).
 * One coming in is len bytes of user space's; one going out is the
 * protocol's (*klen its size), as much of it as fits in user space's
 * (*ulen bytes), *ulen then its size.
 */
union ksockaddr {
    struct sockaddr sa;
    struct sockaddr_un un;
};

static int sockaddr_copyin(union ksockaddr *k, const struct sockaddr *uaddr, socklen_t len)
{
    if (len > sizeof(*k))
        return -EINVAL;

    return copyin(k, uaddr, len);
}

/** user space's room for one going out: its size, at most ours */
static int sockaddr_room(socklen_t *ulen, socklen_t *klen)
{
    if (copyin(klen, ulen, sizeof(*klen)))
        return -EFAULT;

    *klen = MIN(*klen, sizeof(union ksockaddr));
    return 0;
}

static int sockaddr_copyout(struct sockaddr *uaddr, socklen_t *ulen, const union ksockaddr *k, socklen_t klen, socklen_t room)
{
    if (copyout(uaddr, k, MIN(klen, room)) || copyout(ulen, &klen, sizeof(klen)))
        return -EFAULT;

    return 0;
}

static long sys_accept(int fd, const struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "accept(fd=%d, addr=%p, len=%p)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        return -EBADF;
    }

    int conn_fd = proc_fd_get(curproc);

    if (conn_fd == -1) {
        return -EMFILE;
    }

    struct file *conn = file_new();

    if (!conn) {
        proc_fd_release(curproc, conn_fd);
        return -ENOMEM;
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    socklen_t room = 0, klen = 0;
    int err = 0;

    if (addr && (err = sockaddr_room(len, &room))) {
        kfree(conn);
        proc_fd_release(curproc, conn_fd);
        return err;
    }

    klen = room;

    if ((err = socket_accept(socket, conn, addr ? &k.sa : NULL, addr ? &klen : NULL))) {
        kfree(conn);
        proc_fd_release(curproc, conn_fd);
        return err; 
    }

    proc_fd_install(curproc, conn_fd, conn, 0);

    /* nowhere to say who: the connection goes */
    if (addr && sockaddr_copyout((struct sockaddr *) addr, len, &k, klen, room)) {
        proc_fd_close(curproc, conn_fd);
        return -EFAULT;
    }

    return conn_fd;
}

static long sys_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    syscall_log(LOG_DEBUG, "bind(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    int err = 0;

    if ((err = sockaddr_copyin(&k, addr, len)) || (err = socket_bind(file, &k.sa, len))) {
        return err; 
    }

    return 0;
}

static long sys_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    syscall_log(LOG_DEBUG, "connect(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        return -EBADF; 
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    int err = 0;

    if ((err = sockaddr_copyin(&k, addr, len)) || (err = socket_connect(socket, &k.sa, len))) {
        return err; 
    }

    return 0;
}

static long sys_listen(int fd, int backlog)
{
    syscall_log(LOG_DEBUG, "listen(fd=%d, backlog=%d)\n", fd, backlog);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if ((err = socket_listen(file, backlog))) {
        return err; 
    }

    return 0;
}

static long sys_send(struct aquila_socket_io_args *us)
{
    struct aquila_socket_io_args _s, *s = &_s;

    if (copyin(s, us, sizeof(*s))) {
        return -EFAULT;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "send(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* sendto(): where to */
    union ksockaddr to;
    memset(&to, 0, sizeof(to));
    socklen_t tolen = 0;

    if (s->addr) {
        if (copyin(&tolen, s->addrlen, sizeof(tolen))) {
            return -EFAULT;
        }

        if ((err = sockaddr_copyin(&to, s->addr, tolen))) {
            return err;
        }
    }

    struct iobuf io;
    iobuf_init(&io, buf, len, 1);

    ssize_t ret = socket_send(file, &io, flags, s->addr ? &to.sa : NULL, tolen);

    /* to a connection that can't send anymore: SIGPIPE too, as write()
     * (not with MSG_NOSIGNAL) */
    if (ret == -EPIPE && !(flags & MSG_NOSIGNAL))
        signal_proc_send(curproc, SIGPIPE);

    return ret;
}

static long sys_recv(struct aquila_socket_io_args *us)
{
    struct aquila_socket_io_args _s, *s = &_s;

    if (copyin(s, us, sizeof(*s))) {
        return -EFAULT;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "recv(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* recvfrom(): where from */
    union ksockaddr from;
    memset(&from, 0, sizeof(from));
    socklen_t room = 0, fromlen = 0;

    if (s->addr && (err = sockaddr_room(s->addrlen, &room))) {
        return err;
    }

    fromlen = room;

    struct iobuf io;
    iobuf_init(&io, buf, len, 1);

    ssize_t ret = socket_recv(file, &io, flags, s->addr ? &from.sa : NULL, s->addr ? &fromlen : NULL);

    if (ret >= 0 && s->addr && sockaddr_copyout(s->addr, s->addrlen, &from, fromlen, room))
        ret = -EFAULT;

    return ret;
}

/** getsockname(), getpeername() */
static long socket_name(int fd, struct sockaddr *addr, socklen_t *len, int peer)
{
    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    union ksockaddr k;
    memset(&k, 0, sizeof(k));
    socklen_t room, klen;
    int err;

    if ((err = sockaddr_room(len, &room))) {
        return err;
    }

    klen = room;

    if (!(err = socket_getname(file, &k.sa, &klen, peer)))
        err = sockaddr_copyout(addr, len, &k, klen, room);

    return err;
}

static long sys_getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getsockname(fd=%d, addr=%p, len=%p)\n", fd, addr, len);
    return socket_name(fd, addr, len, 0);
}

static long sys_getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getpeername(fd=%d, addr=%p, len=%p)\n", fd, addr, len);
    return socket_name(fd, addr, len, 1);
}

static long sys_getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    syscall_log(LOG_DEBUG, "getsockopt(fd=%d, level=%d, name=%d, val=%p, len=%p)\n",
            fd, level, name, val, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    /* an option's value: the kernel's copy (the biggest is a timeval) */
    char kval[64];
    memset(kval, 0, sizeof(kval));
    socklen_t room, klen;
    int err;

    if (copyin(&room, len, sizeof(room))) {
        return -EFAULT;
    }

    klen = room = MIN(room, sizeof(kval));

    if (!(err = socket_getsockopt(file, level, name, kval, &klen)) &&
        (copyout(val, kval, MIN(klen, room)) || copyout(len, &klen, sizeof(klen))))
        err = -EFAULT;

    return err;
}

static long sys_setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    syscall_log(LOG_DEBUG, "setsockopt(fd=%d, level=%d, name=%d, val=%p, len=%d)\n",
            fd, level, name, val, len);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    char kval[64];

    len = MIN(len, sizeof(kval));

    if (copyin(kval, val, len)) {
        return -EFAULT;
    }

    return socket_setsockopt(file, level, name, kval, len);
}

static long sys_umask(mode_t mask)
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

static long sys_chmod(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "chmod(path=%p, mode=%o)\n", path, mode);
    return do_fchmodat(AT_FDCWD, path, mode, 0);
}

static long sys_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
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

static long sys_fchmod(int fd, mode_t mode)
{
    syscall_log(LOG_DEBUG, "fchmod(fd=%d, mode=%o)\n", fd, mode);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chmod(vnode, mode, &PROC_UIO(curproc));

    return ret;
}

static long sys_sysconf(int name)
{
    syscall_log(LOG_DEBUG, "sysconf(name=%d)\n", name);
    return -ENOSYS;
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

static long sys_access(const char *path, int mode)
{
    syscall_log(LOG_DEBUG, "access(path=%p, mode=%d)\n", path, mode);
    return do_faccessat(AT_FDCWD, path, mode, 0);
}

static long sys_faccessat(int dirfd, const char *path, int mode, int flags)
{
    syscall_log(LOG_DEBUG, "faccessat(dirfd=%d, path=%p, mode=%d, flags=%x)\n", dirfd, path, mode, flags);
    return do_faccessat(dirfd, path, mode, flags);
}

static long sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    syscall_log(LOG_DEBUG, "gettimeofday(tv=%p, tz=%p)\n", tv, tz);

    struct timeval ktv;
    struct timezone ktz;
    int err = gettimeofday(&ktv, &ktz);

    if (!err && ((tv && copyout(tv, &ktv, sizeof(ktv))) || (tz && copyout(tz, &ktz, sizeof(ktz)))))
        err = -EFAULT;

    return err;
}

static long sys_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
    syscall_log(LOG_DEBUG, "sigmask(how=%d, set=%p, oldset=%p)\n", how, set, oldset);

    sigset_t kset, mask = curproc->sigmask;

    if (set && copyin(&kset, set, sizeof(kset))) {
        return -EFAULT;
    }

    if (set) {
        switch (how) {
            case SIG_BLOCK:   mask |= kset;  break;
            case SIG_UNBLOCK: mask &= ~kset; break;
            case SIG_SETMASK: mask = kset;   break;
            default:
                return -EINVAL;
        }
    }

    if (oldset && copyout(oldset, &curproc->sigmask, sizeof(sigset_t))) {
        return -EFAULT;
    }

    /* one no longer blocked is delivered before this returns to user
     * mode */
    curproc->sigmask = mask & ~SIG_UNMASKABLE;
    return 0;
}

static long sys_sigsuspend(const sigset_t *set)
{
    syscall_log(LOG_DEBUG, "sigsuspend(set=%p)\n", set);

    sigset_t kset;

    if (copyin(&kset, set, sizeof(kset))) {
        return -EFAULT;
    }

    struct thread *thread = curthread;
    struct waitq wait = {0};

    /* the mask it replaces is the handler's frame's (sigreturn restores
     * it), or comes back if it's restarted (x86_return_to_user) */
    thread->sig_oldmask = curproc->sigmask;
    thread->sig_oldmask_set = 1;
    curproc->sigmask = kset & ~SIG_UNMASKABLE;

    /* until a signal it doesn't block (nothing else wakes it up) */
    while (!waitq_sleep(&wait))
        ;

    /* EINTR once a handler ran; restarted if none did (a stop) */
    return -ERESTARTNOHAND;
}

/** a handler returns (the kernel's sigcode calls this): its frame back */
static long sys_sigreturn(void)
{
    syscall_log(LOG_DEBUG, "sigreturn()\n");
    arch_sigreturn();

    /* the registers are the frame's now: syscall_dispatch() doesn't set
     * a result */
    return 0;
}

static long sys_getppid(void)
{
    syscall_log(LOG_DEBUG, "getppid()\n");

    /* orphans are init's; init and the kernel's process have no parent */
    return curproc->parent ? curproc->parent->pid : 0;
}

typedef unsigned long fd_mask;

#define  FD_SETSIZE  64
#define   NFDBITS (sizeof (fd_mask) * 8)  /* bits per mask */
#define  _howmany(x,y)   (((x)+((y)-1))/(y))

typedef struct {
    fd_mask fds_bits[_howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

/** whether `file' can be read (or written) without waiting; a file
 * that can't tell is ready, the read or write reports the error */
static int fd_ready(struct file *file, int write)
{
    int ret = write ? vfs_file_can_write(file, 1) : vfs_file_can_read(file, 1);
    return ret != 0;
}

/** select() and poll() wait for `file' (to be read, or written): on the
 * queue its reader (writer) sleeps on, NULL if it can't tell */
static int fd_watch(struct file *file, int write)
{
    return poll_register(vfs_file_wait_queue(file, write));
}

/** deadline of a select()/poll() timeout of `ns' nanoseconds, -1 for
 * none; 0 means don't wait. Restarted after a stop: the deadline it had */
static uint64_t poll_deadline(int64_t ns)
{
    uint64_t until = ns < 0 ? 0 : curthread->restarting ? curthread->restart_until :
                     arch_rtime_ns() + ns;

    curthread->restart_until = until;
    return until;
}

static long sys_select(struct aquila_select_args *args)
{
    syscall_log(LOG_DEBUG, "select(args=%p)\n", args);

    struct aquila_select_args a;

    if (copyin(&a, args, sizeof(a))) {
        return -EFAULT;
    }

    int nfds = a.nfds;
    fd_set *readfds = a.readfds;
    fd_set *writefds = a.writefds;
    fd_set *exceptfds = a.exceptfds;
    sigset_t *sigmask = a.sigmask;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        return -EINVAL;
    }

    /* what was asked (the sets are overwritten with the result), the
     * kernel's copies */
    fd_set want_read = {0}, want_write = {0}, out_read, out_write;
    struct timeval timeout;
    sigset_t mask;

    if ((readfds && copyin(&want_read, readfds, sizeof(fd_set))) ||
        (writefds && copyin(&want_write, writefds, sizeof(fd_set))) ||
        (a.timeout && copyin(&timeout, a.timeout, sizeof(timeout))) ||
        (sigmask && copyin(&mask, sigmask, sizeof(mask)))) {
        return -EFAULT;
    }

    if (a.timeout && ((int64_t) timeout.tv_sec < 0 || timeout.tv_usec < 0 || timeout.tv_usec >= 1000000)) {
        return -EINVAL;
    }

    /* pselect(): the mask while waiting; the old one is back when this
     * returns, or in the handler's frame (as in sigsuspend) */
    if (sigmask) {
        curthread->sig_oldmask = curproc->sigmask;
        curthread->sig_oldmask_set = 1;
        curproc->sigmask = mask & ~SIG_UNMASKABLE;
    }

    int64_t ns = a.timeout ? (int64_t) timeout.tv_sec * 1000000000 + timeout.tv_usec * 1000 : -1;
    uint64_t until = poll_deadline(ns);
    int count, timed_out = 0;

#define FD_BIT(set, i) ((set).fds_bits[(i) / NFDBITS] & (1UL << ((i) % NFDBITS)))
#define FD_ADD(set, i) ((set).fds_bits[(i) / NFDBITS] |= (1UL << ((i) % NFDBITS)))

    for (;;) {
        out_read  = (fd_set) {0};
        out_write = (fd_set) {0};
        count = 0;

        for (int i = 0; i < nfds; ++i) {
            if (!FD_BIT(want_read, i) && !FD_BIT(want_write, i))
                continue;

            struct file *file = fd_peek(i);
            int err = 0;

            if (!file)
                err = -EBADF;

            if (!err && FD_BIT(want_read, i) && !(err = fd_watch(file, 0)) && fd_ready(file, 0)) {
                FD_ADD(out_read, i);
                ++count;
            }

            if (!err && FD_BIT(want_write, i) && !(err = fd_watch(file, 1)) && fd_ready(file, 1)) {
                FD_ADD(out_write, i);
                ++count;
            }

            if (err) {
                poll_unregister(curthread);
                return err;
            }
        }

        if (count || ns == 0 || timed_out) {
            poll_unregister(curthread);
            break;
        }

        /* a wakeup of what one of them waits on makes us look again; a
         * signal ends the wait: EINTR (POSIX: not restarted, but after a
         * stop) */
        timed_out = sched_wait(&curthread->poll_wait, until);
        poll_unregister(curthread);

        if (timed_out < 0) {
            return -ERESTARTNOHAND;
        }
    }

    if (sigmask) {
        curproc->sigmask = curthread->sig_oldmask;
        curthread->sig_oldmask_set = 0;
    }

#undef FD_BIT
#undef FD_ADD

    fd_set none = {0};

    if ((readfds && copyout(readfds, &out_read, sizeof(fd_set))) ||
        (writefds && copyout(writefds, &out_write, sizeof(fd_set))) ||
        (exceptfds && copyout(exceptfds, &none, sizeof(fd_set)))) {
        return -EFAULT;
    }

    return count;
}

static long sys_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    syscall_log(LOG_DEBUG, "poll(fds=%p, nfds=%d, timeout=%d)\n", fds, nfds, timeout_ms);

    if (nfds > FDS_COUNT * 2) {
        return -EINVAL;
    }

    /* the kernel's copy, the events in it, the results out at the end */
    size_t size = nfds * sizeof(struct pollfd);
    struct pollfd *kfds = nfds ? kmalloc(size, &M_BUFFER, 0) : NULL;

    if (nfds && !kfds) {
        return -ENOMEM;
    }

    if (copyin(kfds, fds, size)) {
        kfree(kfds);
        return -EFAULT;
    }

    int64_t ns = timeout_ms < 0 ? -1 : (int64_t) timeout_ms * 1000000;
    uint64_t until = poll_deadline(ns);
    int count, timed_out = 0;

    for (;;) {
        int err = 0;
        count = 0;

        for (nfds_t i = 0; i < nfds; ++i) {
            struct pollfd *p = &kfds[i];
            struct file *file;

            p->revents = 0;

            /* negative descriptors are skipped */
            if (p->fd < 0)
                continue;

            if (!(file = fd_peek(p->fd))) {
                p->revents = POLLNVAL;
            } else {
                if ((p->events & POLLIN) && !err && !(err = fd_watch(file, 0)) && fd_ready(file, 0))
                    p->revents |= POLLIN;

                if ((p->events & POLLOUT) && !err && !(err = fd_watch(file, 1)) && fd_ready(file, 1))
                    p->revents |= POLLOUT;
            }

            count += !!p->revents;
        }

        if (err) {
            poll_unregister(curthread);
            count = err;
            break;
        }

        if (count || ns == 0 || timed_out) {
            poll_unregister(curthread);
            break;
        }

        /* a wakeup of what one of them waits on makes us look again; a
         * signal ends the wait: EINTR (but after a stop) */
        timed_out = sched_wait(&curthread->poll_wait, until);
        poll_unregister(curthread);

        if (timed_out < 0) {
            count = -ERESTARTNOHAND;
            break;
        }
    }

    if (count >= 0 && copyout(fds, kfds, size))
        count = -EFAULT;

    kfree(kfds);
    return count;
}

static long sys_getpgrp(void)
{
    syscall_log(LOG_DEBUG, "getpgrp()\n");
    return curproc->pgrp->pgid;
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

static long sys_chown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "chown(path=%p, owner=%d, group=%d)\n", path, owner, group);
    return do_fchownat(AT_FDCWD, path, owner, group, 0);
}

static long sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    syscall_log(LOG_DEBUG, "fchownat(dirfd=%d, path=%p, owner=%d, group=%d, flags=%x)\n",
            dirfd, path, owner, group, flags);
    return do_fchownat(dirfd, path, owner, group, flags);
}

static long sys_fchown(int fd, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "fchown(fd=%d, owner=%d, group=%d)\n", fd, owner, group);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chown(vnode, owner, group, &PROC_UIO(curproc));

    return ret;
}

static long sys_lchown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "lchown(path=%p, owner=%d, group=%d)\n", path, owner, group);
    return do_fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}

/** `path' NULL: the file `dirfd' refers to (futimens) */
static long sys_utimensat(int dirfd, const char *path, const struct aquila_timespec times[2], int flags)
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

static long sys_rmdir(const char *path)
{
    syscall_log(LOG_DEBUG, "rmdir(path=%p)\n", path);
    return do_unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

static long sys_symlinkat(const char *utarget, int dirfd, const char *upath)
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

static long sys_readlinkat(int dirfd, const char *upath, char *buf, size_t size)
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

static long sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
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

static long sys_flock(int fd, int op)
{
    syscall_log(LOG_DEBUG, "flock(fd=%d, op=%d)\n", fd, op);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    return vfs_file_flock(file, op);
}

static long sys_sync(void)
{
    syscall_log(LOG_DEBUG, "sync()\n");

    /* writes go to the devices right away, only their caches are left */
    return kdev_sync();
}

static long sys_shutdown(int fd, int how)
{
    syscall_log(LOG_DEBUG, "shutdown(fd=%d, how=%d)\n", fd, how);

    struct file *file = fd_file(fd);

    if (!file) {
        return -EBADF;
    }

    return socket_shutdown_dir(file, how);
}

static long sys_setitimer(int which, const struct aquila_itimer *new, struct aquila_itimer *old)
{
    syscall_log(LOG_DEBUG, "setitimer(which=%d, new=%p, old=%p)\n", which, new, old);

    struct aquila_itimer knew;

    if (new && copyin(&knew, new, sizeof(knew))) {
        return -EFAULT;
    }

    /* only the real time timer, the kernel doesn't count virtual time */
    if (which != AQUILA_ITIMER_REAL) {
        return -EINVAL;
    }

    struct aquila_itimer prev = {0};

    /* where the old one goes: writable, before the new one is set */
    if (old && copyout(old, &prev, sizeof(prev))) {
        return -EFAULT;
    }

    if (new)
        sched_alarm(curproc, knew.value, knew.interval, &prev.value, &prev.interval);
    else
        sched_alarm_get(curproc, &prev.value, &prev.interval);

    return old ? copyout(old, &prev, sizeof(prev)) : 0;
}

/** random bytes, as many as asked (getentropy) */
static long sys_getrandom(void *buf, size_t len, unsigned flags __unused)
{
    if (uaccess_check(buf, len)) {
        return -EFAULT;
    }

    /* a piece at a time through the kernel */
    uint8_t kbuf[256];
    size_t done = 0;

    while (done < len) {
        size_t n = MIN(len - done, sizeof(kbuf));
        random_get(kbuf, n);

        if (copyout((char *) buf + done, kbuf, n))
            break;

        done += n;
    }

    memset(kbuf, 0, sizeof(kbuf));
    return done || !len ? (ssize_t) done : -EFAULT;
}

/** the calling thread's id (pthread_self) */
static long sys_thread_self(void)
{
    return curthread->tid;
}

static long sys_reboot(unsigned cmd)
{
    syscall_log(LOG_DEBUG, "reboot(cmd=0x%x)\n", cmd);

    if (curproc->uid) {
        return -EPERM;
    }

    return kernel_reboot(cmd);
}

static long sys_fchdir(int fd)
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

static long sys_setsid(void)
{
    syscall_log(LOG_DEBUG, "setsid()\n");

    struct proc *proc = curproc;
    struct pgroup *pgrp = proc->pgrp;
    struct qnode *pgrp_node = proc->pgrp_node;

    /* a group leader can't leave its group, the group would be in two sessions */
    if (pgrp->pgid == proc->pid) {
        return -EPERM;
    }

    int ret = session_new(proc);

    if (ret) {
        proc->pgrp = pgrp;
        proc->pgrp_node = pgrp_node;
        return ret;
    }

    queue_node_remove(pgrp->procs, pgrp_node);
    pgrp_release(pgrp);
    return proc->pid;
}

static long sys_nanosleep(const struct aquila_timespec *req, struct aquila_timespec *rem)
{
    syscall_log(LOG_DEBUG, "nanosleep(req=%p, rem=%p)\n", req, rem);

    struct aquila_timespec kreq;

    if (copyin(&kreq, req, sizeof(kreq))) {
        return -EFAULT;
    }

    if (kreq.tv_nsec >= 1000000000) {
        return -EINVAL;
    }

    /* until the deadline it had when a stop interrupted it, restarted
     * after the stop; else from now */
    uint64_t until = curthread->restarting ? curthread->restart_until :
                     arch_rtime_ns() + kreq.tv_sec * 1000000000ULL + kreq.tv_nsec;

    curthread->restart_until = until;

    if (sched_sleep(until)) {
        /* a signal: the time left, EINTR once a handler ran (a stop: the
         * call is restarted, until the same deadline) */
        if (rem) {
            uint64_t now = arch_rtime_ns();
            uint64_t left = now < until ? until - now : 0;
            struct aquila_timespec krem = {
                .tv_sec  = left / 1000000000ULL,
                .tv_nsec = left % 1000000000ULL,
            };

            if (copyout(rem, &krem, sizeof(krem))) {
                return -EFAULT;
            }
        }

        return -ERESTARTNOHAND;
    }

    return 0;
}

static long sys_clock_gettime(int clock, struct aquila_timespec *ts)
{
    syscall_log(LOG_DEBUG, "clock_gettime(clock=%d, ts=%p)\n", clock, ts);

    struct aquila_timespec kts;
    struct timespec now;
    uint64_t ns;

    switch (clock) {
        case CLOCK_REALTIME:
            gettime(&now);
            kts.tv_sec  = now.tv_sec;
            kts.tv_nsec = now.tv_nsec;
            break;
        case CLOCK_MONOTONIC:
            ns = arch_rtime_ns();
            kts.tv_sec  = ns / 1000000000ULL;
            kts.tv_nsec = ns % 1000000000ULL;
            break;
        default:
            return -EINVAL;
    }

    return copyout(ts, &kts, sizeof(kts));
}

static long sys_sethostname(const char *name, size_t len)
{
    syscall_log(LOG_DEBUG, "sethostname(name=%p, len=%d)\n", name, len);

    if (curproc->uid != 0) {
        return -EPERM;
    }

    if (len >= sizeof(nodename)) {
        return -EINVAL;
    }

    char name_in[sizeof(nodename)];

    if (copyin(name_in, name, len)) {
        return -EFAULT;
    }

    memcpy(nodename, name_in, len);
    nodename[len] = '\0';
    return 0;
}

/*
 * The table: what each system call number calls. Its entries all have
 * the one type the dispatch calls through, five register-sized
 * arguments and a long result (syscall_t); a system call is written
 * with its own types, sys_read(int fd, void *buf, size_t nbytes).
 * SYSCALLn(name, types...) makes the entry for sys_<name>, of n
 * arguments: sys_<name>_entry(), which converts the registers to those
 * types and calls it, so the compiler checks the call. Calling sys_read
 * itself through a pointer of another type would be undefined in C (it
 * happened to work with x86's and aarch64's calling conventions).
 */

#define SYSCALL_ARGS \
    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5

#define SYSCALL0(name) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name(); }
#define SYSCALL1(name, t1) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name((t1) a1); }
#define SYSCALL2(name, t1, t2) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name((t1) a1, (t2) a2); }
#define SYSCALL3(name, t1, t2, t3) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name((t1) a1, (t2) a2, (t3) a3); }
#define SYSCALL4(name, t1, t2, t3, t4) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name((t1) a1, (t2) a2, (t3) a3, (t4) a4); }
#define SYSCALL5(name, t1, t2, t3, t4, t5) \
    static long sys_##name##_entry(SYSCALL_ARGS) \
    { return sys_##name((t1) a1, (t2) a2, (t3) a3, (t4) a4, (t5) a5); }

SYSCALL1(exit, int)
SYSCALL1(close, int)
SYSCALL3(execve, const char *, char * const *, char * const *)
SYSCALL0(fork)
SYSCALL2(fstat, int, struct aquila_stat *)
SYSCALL0(getpid)
SYSCALL1(isatty, int)
SYSCALL2(kill, pid_t, int)
SYSCALL2(link, const char *, const char *)
SYSCALL5(linkat, int, const char *, int, const char *, int)
SYSCALL3(lseek, int, off_t, int)
SYSCALL3(open, const char *, int, mode_t)
SYSCALL4(openat, int, const char *, int, mode_t)
SYSCALL3(read, int, void *, size_t)
SYSCALL1(sbrk, ptrdiff_t)
SYSCALL2(stat, const char *, struct aquila_stat *)
SYSCALL4(fstatat, int, const char *, struct aquila_stat *, int)
SYSCALL1(times, struct aquila_tms *)
SYSCALL1(unlink, const char *)
SYSCALL3(unlinkat, int, const char *, int)
SYSCALL3(waitpid, int, int *, int)
SYSCALL3(write, int, void *, size_t)
SYSCALL3(ioctl, int, int, void *)
SYSCALL3(sigaction, int, const struct aquila_sigaction *, struct aquila_sigaction *)
SYSCALL2(readdir, int, struct dirent *)
SYSCALL1(mount, struct aquila_mount_args *)
SYSCALL2(mkdir, const char *, mode_t)
SYSCALL1(uname, struct utsname *)
SYSCALL1(pipe, int *)
SYSCALL3(fcntl, int, int, uintptr_t)
SYSCALL1(chdir, const char *)
SYSCALL2(getcwd, char *, size_t)
SYSCALL1(thread_create, struct aquila_thread_args *)
SYSCALL1(thread_exit, void *)
SYSCALL2(thread_join, int, void **)
SYSCALL2(setpgid, pid_t, pid_t)
SYSCALL3(mknod, const char *, uint32_t, uint32_t)
SYSCALL4(mknodat, int, const char *, uint32_t, uint32_t)
SYSCALL2(lstat, const char *, struct aquila_stat *)
SYSCALL1(setuid, uid_t)
SYSCALL2(ftruncate, int, off_t)
SYSCALL1(setgid, gid_t)
SYSCALL0(getuid)
SYSCALL0(getgid)
SYSCALL2(mmap, struct aquila_mmap_args *, void **)
SYSCALL2(munmap, void *, size_t)
SYSCALL3(socket, int, int, int)
SYSCALL3(accept, int, const struct sockaddr *, socklen_t *)
SYSCALL3(bind, int, const struct sockaddr *, socklen_t)
SYSCALL3(connect, int, const struct sockaddr *, socklen_t)
SYSCALL2(listen, int, int)
SYSCALL1(send, struct aquila_socket_io_args *)
SYSCALL1(recv, struct aquila_socket_io_args *)
SYSCALL3(getsockname, int, struct sockaddr *, socklen_t *)
SYSCALL3(getpeername, int, struct sockaddr *, socklen_t *)
SYSCALL5(getsockopt, int, int, int, void *, socklen_t *)
SYSCALL5(setsockopt, int, int, int, const void *, socklen_t)
SYSCALL1(umask, mode_t)
SYSCALL2(chmod, const char *, mode_t)
SYSCALL4(fchmodat, int, const char *, mode_t, int)
SYSCALL2(fchmod, int, mode_t)
SYSCALL1(sysconf, int)
SYSCALL2(access, const char *, int)
SYSCALL4(faccessat, int, const char *, int, int)
SYSCALL2(gettimeofday, struct timeval *, struct timezone *)
SYSCALL3(sigmask, int, const sigset_t *, sigset_t *)
SYSCALL1(sigsuspend, const sigset_t *)
SYSCALL0(sigreturn)
SYSCALL0(getppid)
SYSCALL1(select, struct aquila_select_args *)
SYSCALL3(poll, struct pollfd *, nfds_t, int)
SYSCALL0(getpgrp)
SYSCALL3(chown, const char *, uid_t, gid_t)
SYSCALL5(fchownat, int, const char *, uid_t, gid_t, int)
SYSCALL3(fchown, int, uid_t, gid_t)
SYSCALL3(lchown, const char *, uid_t, gid_t)
SYSCALL4(utimensat, int, const char *, const struct aquila_timespec *, int)
SYSCALL1(rmdir, const char *)
SYSCALL3(symlinkat, const char *, int, const char *)
SYSCALL4(readlinkat, int, const char *, char *, size_t)
SYSCALL4(renameat, int, const char *, int, const char *)
SYSCALL2(flock, int, int)
SYSCALL0(sync)
SYSCALL2(shutdown, int, int)
SYSCALL3(setitimer, int, const struct aquila_itimer *, struct aquila_itimer *)
SYSCALL3(getrandom, void *, size_t, unsigned)
SYSCALL0(thread_self)
SYSCALL1(reboot, unsigned)
SYSCALL1(fchdir, int)
SYSCALL0(setsid)
SYSCALL2(nanosleep, const struct aquila_timespec *, struct aquila_timespec *)
SYSCALL2(clock_gettime, int, struct aquila_timespec *)
SYSCALL2(sethostname, const char *, size_t)

const syscall_t syscall_table[] = {
    [SYS_exit]          = sys_exit_entry,
    [SYS_close]         = sys_close_entry,
    [SYS_execve]        = sys_execve_entry,
    [SYS_fork]          = sys_fork_entry,
    [SYS_fstat]         = sys_fstat_entry,
    [SYS_getpid]        = sys_getpid_entry,
    [SYS_isatty]        = sys_isatty_entry,
    [SYS_kill]          = sys_kill_entry,
    [SYS_link]          = sys_link_entry,
    [SYS_lseek]         = sys_lseek_entry,
    [SYS_open]          = sys_open_entry,
    [SYS_read]          = sys_read_entry,
    [SYS_sbrk]          = sys_sbrk_entry,
    [SYS_stat]          = sys_stat_entry,
    [SYS_times]         = sys_times_entry,
    [SYS_unlink]        = sys_unlink_entry,
    [SYS_waitpid]       = sys_waitpid_entry,
    [SYS_write]         = sys_write_entry,
    [SYS_ioctl]         = sys_ioctl_entry,
    [SYS_sigaction]     = sys_sigaction_entry,
    [SYS_readdir]       = sys_readdir_entry,
    [SYS_mount]         = sys_mount_entry,
    [SYS_mkdir]         = sys_mkdir_entry,
    [SYS_uname]         = sys_uname_entry,
    [SYS_pipe]          = sys_pipe_entry,
    [SYS_fcntl]         = sys_fcntl_entry,
    [SYS_chdir]         = sys_chdir_entry,
    [SYS_getcwd]        = sys_getcwd_entry,
    [SYS_thread_create] = sys_thread_create_entry,
    [SYS_thread_exit]   = sys_thread_exit_entry,
    [SYS_thread_join]   = sys_thread_join_entry,
    [SYS_setpgid]       = sys_setpgid_entry,
    [SYS_mknod]         = sys_mknod_entry,
    [SYS_lstat]         = sys_lstat_entry,
    [SYS_getuid]        = sys_getuid_entry,
    [SYS_getgid]        = sys_getgid_entry,
    [SYS_mmap]          = sys_mmap_entry,
    [SYS_munmap]        = sys_munmap_entry,
    [SYS_socket]        = sys_socket_entry,
    [SYS_accept]        = sys_accept_entry,
    [SYS_bind]          = sys_bind_entry,
    [SYS_connect]       = sys_connect_entry,
    [SYS_listen]        = sys_listen_entry,
    [SYS_send]          = sys_send_entry,
    [SYS_recv]          = sys_recv_entry,
    [SYS_umask]         = sys_umask_entry,
    [SYS_chmod]         = sys_chmod_entry,
    [SYS_sysconf]       = sys_sysconf_entry,
    [SYS_gettimeofday]  = sys_gettimeofday_entry,
    [SYS_access]        = sys_access_entry,
    [SYS_sigmask]       = sys_sigmask_entry,
    [SYS_select]        = sys_select_entry,
    [SYS_getpgrp]       = sys_getpgrp_entry,
    [SYS_chown]         = sys_chown_entry,
    [SYS_fchown]        = sys_fchown_entry,
    [SYS_lchown]        = sys_lchown_entry,
    [SYS_rmdir]         = sys_rmdir_entry,
    [SYS_setuid]        = sys_setuid_entry,
    [SYS_setgid]        = sys_setgid_entry,
    [SYS_ftruncate]     = sys_ftruncate_entry,
    [SYS_sigsuspend]    = sys_sigsuspend_entry,
    [SYS_getppid]       = sys_getppid_entry,
    [SYS_openat]        = sys_openat_entry,
    [SYS_fstatat]       = sys_fstatat_entry,
    [SYS_unlinkat]      = sys_unlinkat_entry,
    [SYS_mknodat]       = sys_mknodat_entry,
    [SYS_linkat]        = sys_linkat_entry,
    [SYS_symlinkat]     = sys_symlinkat_entry,
    [SYS_readlinkat]    = sys_readlinkat_entry,
    [SYS_renameat]      = sys_renameat_entry,
    [SYS_faccessat]     = sys_faccessat_entry,
    [SYS_fchmodat]      = sys_fchmodat_entry,
    [SYS_fchownat]      = sys_fchownat_entry,
    [SYS_utimensat]     = sys_utimensat_entry,
    [SYS_fchmod]        = sys_fchmod_entry,
    [SYS_fchdir]        = sys_fchdir_entry,
    [SYS_setsid]        = sys_setsid_entry,
    [SYS_nanosleep]     = sys_nanosleep_entry,
    [SYS_clock_gettime] = sys_clock_gettime_entry,
    [SYS_sethostname]   = sys_sethostname_entry,
    [SYS_poll]          = sys_poll_entry,
    [SYS_flock]         = sys_flock_entry,
    [SYS_reboot]        = sys_reboot_entry,
    [SYS_sync]          = sys_sync_entry,
    [SYS_shutdown]      = sys_shutdown_entry,
    [SYS_setitimer]     = sys_setitimer_entry,
    [SYS_thread_self]   = sys_thread_self_entry,
    [SYS_getrandom]     = sys_getrandom_entry,
    [SYS_getsockname]   = sys_getsockname_entry,
    [SYS_getpeername]   = sys_getpeername_entry,
    [SYS_getsockopt]    = sys_getsockopt_entry,
    [SYS_setsockopt]    = sys_setsockopt_entry,
    [SYS_sigreturn]     = sys_sigreturn_entry,
};

const size_t syscall_cnt = sizeof(syscall_table)/sizeof(syscall_table[0]);


/**
 * A system call from user mode (the architecture's arch_syscall(): the
 * number and the arguments, as they came in registers). Its result is
 * the thread's to return to user mode (arch_syscall_return()), but
 * sigreturn's: its registers are the signal frame's it restored. A call
 * that doesn't return (exit, an exec that worked) doesn't come back here.
 */
void syscall_dispatch(unsigned long nr, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                      uintptr_t a4, uintptr_t a5)
{
    curthread->syscall_nr = nr;

    if (nr >= syscall_cnt || !syscall_table[nr]) {
        printk("[%d:%d] %s: undefined syscall %ld\n", curproc->pid, curthread->tid,
               curproc->name, (long) nr);
        arch_syscall_return(curthread, -ENOSYS);
        return;
    }

    long ret = syscall_table[nr](a1, a2, a3, a4, a5);

    if (nr != SYS_sigreturn)
        arch_syscall_return(curthread, ret);

    /* the open files the call used: let go of */
    thread_files_release(curthread);

    /* a restart was this call, or none */
    curthread->restarting = 0;
}
