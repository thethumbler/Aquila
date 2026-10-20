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

#include <net/socket.h>

#include <aquila/errno.h>
#include <aquila/dirent.h>
#include <aquila/utsname.h>
#include <aquila/fcntl.h>
#include <aquila/mman.h>
#include <aquila/time.h>

#include <fs/devpts.h>
#include <fs/pipe.h>
#include <fs/stat.h>

#include <mm/vm.h>

static int syscall_log_level = LOG_NONE;
LOGGER_DEFINE(syscall, syscall_log, syscall_log_level)

/* return the open file associated with `fd', NULL if `fd' is not open */
static struct file *fd_file(int fd)
{
    if (fd < 0 || fd >= FDS_COUNT)  /* Out of bounds */
        return NULL;

    struct file *file = &curproc->fds[fd];

    /* free or reserved (being set up by another syscall) descriptor */
    if (!file->vnode || file->vnode == (void *) -1)
        return NULL;

    return file;
}

/*
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

    /* only directories opened by path know where they are */
    if ((file->flags & FILE_SOCKET) || !S_ISDIR(file->vnode->mode) || !file->path)
        return -ENOTDIR;

    uio->cwd = file->path;
    return 0;
}

/* absolute path of `path' from directory `dirfd', for calls taking two paths */
static int at_path(int dirfd, const char *path, char **abs_path)
{
    struct uio uio;
    int ret;

    if (uaccess_str_check(path))
        return -EFAULT;

    if ((ret = at_uio(dirfd, path, &uio)))
        return ret;

    return vfs_parse_path(path, &uio, abs_path);
}

/* look up `path' from `dirfd', AT_SYMLINK_NOFOLLOW in flags: don't follow a final link */
static int at_lookup(int dirfd, const char *path, int flags, struct uio *uio, struct vnode **vnode)
{
    int ret;

    if (uaccess_str_check(path))
        return -EFAULT;

    if ((ret = at_uio(dirfd, path, uio)))
        return ret;

    if (flags & AT_SYMLINK_NOFOLLOW)
        uio->flags |= O_NOFOLLOW;

    return vfs_lookup(path, uio, vnode, NULL);
}

static void sys_exit(int code)
{
    syscall_log(LOG_DEBUG, "exit(code=%d)\n", code);

    struct proc *owner = curproc;

    owner->exit = PROC_EXIT(code, 0);  /* Child exited normally */

    proc_kill(owner);
    arch_sleep();

    /* We should never reach this anyway */
    for (;;);
}

static void sys_close(int fildes)
{
    syscall_log(LOG_DEBUG, "close(fildes=%d)\n", fildes);

    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    int ret = vfs_file_close(file);

    /* clear the slot so no stale offset/flags leak into the next user */
    *file = (struct file) {0};

    arch_syscall_return(curthread, ret);
}

static void sys_execve(const char *path, char * const argp[], char * const envp[])
{
    syscall_log(LOG_DEBUG, "execve(path=%s, argp=%p, envp=%p)\n", path, argp, envp);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (!*path) {
        arch_syscall_return(curthread, -ENOENT);
        return;
    }

    int err = 0;
    char *fn = strdup(path);

    if (!fn) {
        arch_syscall_return(curthread, -ENOMEM);
        return;
    }

    err = proc_execve(curthread, fn, argp, envp);

    kfree(fn);

    if (err) {
        arch_syscall_return(curthread, err);
    } else {
        sched_thread_spawn(curthread);
    }
}

static void sys_fork(void)
{
    syscall_log(LOG_DEBUG, "fork()\n");

    struct proc *fork = NULL;
    int err = proc_fork(curthread, &fork);

    if (err) {
        arch_syscall_return(curthread, err);
        return;
    }

    /* Returns are handled inside proc_fork */
    if (fork != NULL) {
        struct thread *thread = (struct thread *) fork->threads.head->value;
        sched_thread_ready(thread);
    }
}

static void sys_fstat(int fildes, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "fstat(fildes=%d, buf=%p)\n", fildes, buf);

    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (uaccess_check(buf, sizeof(struct aquila_stat))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        /* sockets have no vnode, report a minimal stat */
        memset(buf, 0, sizeof(struct aquila_stat));
        buf->st_mode = S_IFSOCK;
        arch_syscall_return(curthread, 0);
        return;
    }

    struct vnode *vnode = file->vnode;

    int ret = vfs_stat(vnode, buf);
    arch_syscall_return(curthread, ret);
}

static void sys_getpid(void)
{
    syscall_log(LOG_DEBUG, "getpid()\n");
    arch_syscall_return(curthread, curproc->pid);
}

static void sys_isatty(int fildes)
{
    syscall_log(LOG_DEBUG, "isatty(fildes=%d)\n", fildes);

    struct file *file = fd_file(fildes);

    if (!file) {    /* Invalid File Descriptor */
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    /* only character devices can be terminals (libc's isatty() asks the
     * device with TCGETS instead) */
    if ((file->flags & FILE_SOCKET) || !S_ISCHR(file->vnode->mode)) {
        arch_syscall_return(curthread, -ENOTTY);
        return;
    }

    arch_syscall_return(curthread, 1);
}

static void sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);

    /* a signal sent to ourselves may be delivered right away and the
     * handler returns to the saved user context, so set the return value
     * before sending it */
    arch_syscall_return(curthread, 0);

    int ret = signal_send(pid, sig);

    if (ret)
        arch_syscall_return(curthread, ret);
}

static int do_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    char *oabs = NULL, *nabs = NULL;
    int ret;

    if (flags & ~AT_SYMLINK_FOLLOW)
        return -EINVAL;

    if (!(ret = at_path(olddirfd, oldpath, &oabs)) &&
        !(ret = at_path(newdirfd, newpath, &nabs))) {
        struct uio uio = PROC_UIO(curproc);

        /* a link to a symbolic link, not to what it points to */
        if (!(flags & AT_SYMLINK_FOLLOW))
            uio.flags = O_NOFOLLOW;

        ret = vfs_link(oabs, nabs, &uio);
    }

    kfree(oabs);
    kfree(nabs);
    return ret;
}

static void sys_link(const char *oldpath, const char *newpath)
{
    syscall_log(LOG_DEBUG, "link(oldpath=%s, newpath=%s)\n", oldpath, newpath);
    arch_syscall_return(curthread, do_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0));
}

static void sys_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
    syscall_log(LOG_DEBUG, "linkat(olddirfd=%d, oldpath=%s, newdirfd=%d, newpath=%s, flags=%x)\n",
            olddirfd, oldpath, newdirfd, newpath, flags);
    arch_syscall_return(curthread, do_linkat(olddirfd, oldpath, newdirfd, newpath, flags));
}

static void sys_lseek(int fildes, off_t offset, int whence)
{
    syscall_log(LOG_DEBUG, "lseek(fildes=%d, offset=%d, whence=%d)\n",
            fildes, offset, whence);

    struct file *file = fd_file(fildes);

    if (!file) {    /* Invalid File Descriptor */
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        arch_syscall_return(curthread, -ESPIPE);
        return;
    }

    int ret = vfs_file_lseek(file, offset, whence);
    arch_syscall_return(curthread, ret);
}

static int do_openat(int dirfd, const char *path, int oflags, mode_t mode)
{
    struct uio uio;
    int ret;

    if (uaccess_str_check(path))
        return -EFAULT;

    if ((ret = at_uio(dirfd, path, &uio)))
        return ret;

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1)   /* Reached maximum number of open file descriptors */
        return -EMFILE;

    struct vnode *vnode = NULL;
    char *abs_path = NULL;
    int excl = (oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL);
    int created = 0;

    uio.flags = oflags;

    /* O_EXCL never creates through a symbolic link */
    if (excl)
        uio.flags |= O_NOFOLLOW;

    ret = vfs_lookup(path, &uio, &vnode, &abs_path);

    if (!ret && excl) {
        ret = -EEXIST;
        goto done;
    }

    if (ret == -ENOENT && (oflags & O_CREAT)) {
        if ((ret = vfs_creat(path, mode & 07777, &uio, &vnode)))
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

    curproc->fds[fd] = (struct file) {
        .vnode   = vnode,
        .offset  = 0,
        .flags   = oflags & ~O_CLOEXEC,
        .fdflags = (oflags & O_CLOEXEC) ? FD_CLOEXEC : 0,
    };

    /* the creator may open the file whatever mode it was given */
    if (!created && (ret = vfs_perms_check(&curproc->fds[fd], &uio)))
        goto done;

    if ((ret = vfs_file_open(&curproc->fds[fd])))
        goto done;

    /* directories remember their path, for the *at() calls */
    if (S_ISDIR(vnode->mode)) {
        curproc->fds[fd].path = abs_path;
        abs_path = NULL;
    }

    ret = fd;

done:
    if (ret < 0) {
        proc_fd_release(curproc, fd);

        if (vnode)
            vfs_close(vnode);
    }

    kfree(abs_path);
    return ret;
}

static void sys_open(const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "open(path=%s, oflags=0x%x, mode=0x%x)\n", path, oflags, mode);
    arch_syscall_return(curthread, do_openat(AT_FDCWD, path, oflags, mode));
}

static void sys_openat(int dirfd, const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "openat(dirfd=%d, path=%s, oflags=0x%x, mode=0x%x)\n", dirfd, path, oflags, mode);
    arch_syscall_return(curthread, do_openat(dirfd, path, oflags, mode));
}

static void sys_read(int fildes, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "read(fd=%d, buf=%p, count=%d)\n", fildes, buf, nbytes);

    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (uaccess_check(buf, nbytes)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int ret = vfs_file_read(file, buf, nbytes);
    arch_syscall_return(curthread, ret);
    return;
}

static void sys_sbrk(ptrdiff_t incr)
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
        arch_syscall_return(curthread, (uintptr_t) -1);
        return;
    }

    curproc->heap = heap + incr;
    curproc->heap_vm->size = PAGE_ROUND(heap + incr - heap_start);

    arch_syscall_return(curthread, heap);
    return;
}

static int do_fstatat(int dirfd, const char *path, struct aquila_stat *buf, int flags)
{
    struct vnode *vnode = NULL;
    struct uio uio;
    int ret;

    if (flags & ~AT_SYMLINK_NOFOLLOW)
        return -EINVAL;

    if (uaccess_check(buf, sizeof(struct aquila_stat)))
        return -EFAULT;

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        return ret;

    ret = vfs_stat(vnode, buf);
    vfs_close(vnode);
    return ret;
}

static void sys_stat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "stat(path=%s, buf=%p)\n", path, buf);
    arch_syscall_return(curthread, do_fstatat(AT_FDCWD, path, buf, 0));
}

static void sys_fstatat(int dirfd, const char *path, struct aquila_stat *buf, int flags)
{
    syscall_log(LOG_DEBUG, "fstatat(dirfd=%d, path=%s, buf=%p, flags=%x)\n", dirfd, path, buf, flags);
    arch_syscall_return(curthread, do_fstatat(dirfd, path, buf, flags));
}

static void sys_times(struct aquila_tms *buf)
{
    syscall_log(LOG_DEBUG, "times(buf=%p)\n", buf);

    const uint64_t tick = 1000000000ULL / AQUILA_CLK_TCK;   /* ns */

    if (uaccess_check(buf, sizeof(struct aquila_tms))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    buf->tms_utime  = curproc->utime / tick;
    buf->tms_stime  = curproc->stime / tick;
    buf->tms_cutime = curproc->cutime / tick;
    buf->tms_cstime = curproc->cstime / tick;

    /* elapsed real time, since boot */
    arch_syscall_return(curthread, (uint32_t) (arch_rtime_ns() / tick));
}

static int do_unlinkat(int dirfd, const char *path, int flags)
{
    struct uio uio;
    int ret;

    if (flags & ~AT_REMOVEDIR)
        return -EINVAL;

    if (uaccess_str_check(path))
        return -EFAULT;

    if ((ret = at_uio(dirfd, path, &uio)))
        return ret;

    return (flags & AT_REMOVEDIR) ? vfs_rmdir(path, &uio) : vfs_unlink(path, &uio);
}

static void sys_unlink(const char *path)
{
    syscall_log(LOG_DEBUG, "unlink(path=%p)\n", path);
    arch_syscall_return(curthread, do_unlinkat(AT_FDCWD, path, 0));
}

static void sys_unlinkat(int dirfd, const char *path, int flags)
{
    syscall_log(LOG_DEBUG, "unlinkat(dirfd=%d, path=%p, flags=%x)\n", dirfd, path, flags);
    arch_syscall_return(curthread, do_unlinkat(dirfd, path, flags));
}

#define WNOHANG 1
static void sys_waitpid(int pid, int *stat_loc, int options)
{
    syscall_log(LOG_DEBUG, "waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", pid, stat_loc, options);

    int nohang = options & WNOHANG;

    if (stat_loc && uaccess_check(stat_loc, sizeof(int))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (pid <= 0) {
        /* pid == -1: wait for any child process
         * pid == 0:  wait for any child process whose process group ID
         *            is equal to that of the calling process
         * pid < -1:  wait for any child process whose process group ID
         *            is equal to the absolute value of pid */
        pid_t pgid = pid == 0? curproc->pgrp->pgid : -pid;

        for (;;) {
            int found = 0;

            queue_for (node, procs) {
                struct proc *proc = node->value;

                if (proc->parent != curproc)
                    continue;

                if (pid != -1 && proc->pgrp->pgid != pgid)
                    continue;

                found = 1;

                if (!proc->running) {
                    if (stat_loc)
                        *stat_loc = proc->exit;

                    arch_syscall_return(curthread, proc->pid);
                    proc_reap(proc);
                    return;
                }
            }

            if (nohang) {
                arch_syscall_return(curthread, found? 0 : -ECHILD);
                return;
            }

            if (thread_queue_sleep(&curproc->wait_queue)) {
                arch_syscall_return(curthread, -EINTR);
                return;
            }
        }

    } else {
        /* wait for the child whose process ID is equal to the
              value of pid */
        struct proc *child = proc_pid_find(pid);

        /* If pid is invalid or current process is not parent of child */
        if (child == NULL || child->parent != curproc) {
            arch_syscall_return(curthread, -ECHILD);
            return;
        }

        if (!(child->running)) {  /* Child is killed */
            if (stat_loc)
                *stat_loc = child->exit;

            arch_syscall_return(curthread, child->pid);
            proc_reap(child);
            return;
        }

        if (nohang) {
            arch_syscall_return(curthread, 0);
            return;
        }

        while (child->running) {
            if (thread_queue_sleep(&curproc->wait_queue)) {
                arch_syscall_return(curthread, -EINTR);
                return;
            }
        }

        if (stat_loc)
            *stat_loc = child->exit;

        arch_syscall_return(curthread, child->pid);
        proc_reap(child);
    }
}

static void sys_write(int fd, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "write(fd=%d, buf=%p, nbytes=%d)\n", fd, buf, nbytes);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (uaccess_check(buf, nbytes)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int ret = vfs_file_write(file, buf, nbytes);
    arch_syscall_return(curthread, ret);

    /* writing to a pipe no one reads: SIGPIPE, the write fails with
     * EPIPE if it is caught or ignored (the return value is set first,
     * a handler may run right away) */
    if (ret == -EPIPE)
        signal_proc_send(curproc, SIGPIPE);
}

static void sys_ioctl(int fd, int request, void *argp)
{
    syscall_log(LOG_DEBUG, "ioctl(fd=%d, request=0x%x, argp=%p)\n",
            fd, request, argp);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        arch_syscall_return(curthread, -ENOTTY);
        return;
    }

    int ret = vfs_file_ioctl(file, request, argp);
    arch_syscall_return(curthread, ret);
    return;
}

static void sys_sigaction(int sig, const struct aquila_sigaction *act, struct aquila_sigaction *oact)
{
    syscall_log(LOG_DEBUG, "sigaction(sig=%d, act=%p, oact=%p)\n",
            sig, act, oact);

    if (sig < 1 || sig > SIG_MAX) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    if ((act && uaccess_check(act, sizeof(struct aquila_sigaction))) ||
        (oact && uaccess_check(oact, sizeof(struct aquila_sigaction)))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (act) {
        uintptr_t handler = act->sa_handler;

        /* SIGKILL and SIGSTOP can not be caught or ignored */
        if ((sig == SIGKILL || sig == SIGSTOP) && handler != SIG_DFL) {
            arch_syscall_return(curthread, -EINVAL);
            return;
        }

        /* handlers must live in user space */
        if (handler != SIG_DFL && handler != SIG_IGN && uaccess_check((void *) handler, 1)) {
            arch_syscall_return(curthread, -EFAULT);
            return;
        }
    }

    if (oact)
        memcpy(oact, &curproc->sigaction[sig], sizeof(struct aquila_sigaction));

    if (act)
        memcpy(&curproc->sigaction[sig], act, sizeof(struct aquila_sigaction));

    arch_syscall_return(curthread, 0);
}

static void sys_readdir(int fd, struct dirent *dirent)
{
    syscall_log(LOG_DEBUG, "readdir(fd=%d, dirent=%p)\n", fd, dirent);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        arch_syscall_return(curthread, -ENOTDIR);
        return;
    }

    if (uaccess_check(dirent, sizeof(struct dirent))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int ret = vfs_file_readdir(file, dirent);

    /* filesystems don't report entry types yet */
    if (ret > 0)
        dirent->d_type = DT_UNKNOWN;

    arch_syscall_return(curthread, ret);
    return;
}

static void sys_mount(struct aquila_mount_args *args)
{
    if (curproc->uid != 0) {
        arch_syscall_return(curthread, -EACCES);
        return;
    }

    if (uaccess_check(args, sizeof(struct aquila_mount_args))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    const char *type = args->type;
    const char *dir  = args->dir;
    int flags = args->flags;
    void *data = args->data;

    syscall_log(LOG_DEBUG, "mount(type=%s, dir=%s, flags=%x, data=%p)\n",
            type, dir, flags, data);

    if (uaccess_str_check(type) || uaccess_str_check(dir)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (data) {
        /* mount data is {char *dev; char *opt;} for all filesystems */
        char **mdata = data;

        if (uaccess_check(mdata, 2 * sizeof(char *)) ||
            (mdata[0] && uaccess_str_check(mdata[0])) ||
            (mdata[1] && uaccess_str_check(mdata[1]))) {
            arch_syscall_return(curthread, -EFAULT);
            return;
        }
    }

    int ret = vfs_mount(type, dir, flags, data, &PROC_UIO(curproc));
    arch_syscall_return(curthread, ret);
    return;
}

static int do_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    struct uio uio;
    int ret;

    if (uaccess_str_check(path))
        return -EFAULT;

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

    if ((ret = at_uio(dirfd, path, &uio)))
        return ret;

    return vfs_mknod(path, mode, dev, &uio, NULL);
}

static void sys_mkdir(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "mkdir(path=%s, mode=%x)\n", path, mode);
    arch_syscall_return(curthread, do_mknodat(AT_FDCWD, path, S_IFDIR | (mode & 07777), 0));
}

/* set by sethostname(), shown by uname() */
static char nodename[sizeof(((struct utsname *) 0)->nodename)] = UTSNAME_NODENAME;

static void sys_uname(struct utsname *name)
{
    syscall_log(LOG_DEBUG, "uname(name=%p)\n", name);

    if (uaccess_check(name, sizeof(struct utsname))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    strcpy(name->sysname,  UTSNAME_SYSNAME);
    strcpy(name->nodename, nodename);
    strcpy(name->release,  UTSNAME_RELEASE);
    strcpy(name->version,  UTSNAME_VERSION);
    strcpy(name->machine,  UTSNAME_MACHINE);

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_pipe(int fd[2])
{
    syscall_log(LOG_DEBUG, "pipe(fd=%p)\n", fd);

    if (uaccess_check(fd, 2 * sizeof(int))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int err = 0;
    int fd1 = -1, fd2 = -1;

    if ((fd1 = proc_fd_get(curproc)) == -1 || (fd2 = proc_fd_get(curproc)) == -1) {
        err = -EMFILE;
        goto error;
    }

    if ((err = pipefs_pipe(&curproc->fds[fd1], &curproc->fds[fd2])))
        goto error;

    fd[0] = fd1;
    fd[1] = fd2;
    arch_syscall_return(curthread, 0);
    return;

error:
    if (fd1 != -1)
        proc_fd_release(curproc, fd1);

    if (fd2 != -1)
        proc_fd_release(curproc, fd2);

    arch_syscall_return(curthread, err);
}

static void sys_fcntl(int fd, int cmd, uintptr_t arg)
{
    syscall_log(LOG_DEBUG, "fcntl(fd=%d, cmd=%d, arg=0x%x)\n",
            fd, cmd, arg);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    /* file status flags that F_SETFL may change */
    const int setfl = O_APPEND | O_NONBLOCK;
    int dupfd = 0;

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            if (arg >= FDS_COUNT) {
                arch_syscall_return(curthread, -EINVAL);
                return;
            }

            /* lowest numbered free descriptor greater than or equal to arg */
            for (dupfd = arg; dupfd < FDS_COUNT; ++dupfd) {
                if (!curproc->fds[dupfd].vnode)
                    break;
            }

            if (dupfd == FDS_COUNT) {
                arch_syscall_return(curthread, -EMFILE);
                return;
            }

            curproc->fds[dupfd] = *file;
            curproc->fds[dupfd].fdflags = cmd == F_DUPFD_CLOEXEC ? FD_CLOEXEC : 0;

            if (file->path)
                curproc->fds[dupfd].path = strdup(file->path);

            /* the new descriptor holds its own reference (as in fork) */
            if (file->flags & FILE_SOCKET)
                file->socket->ref++;
            else
                file->vnode->ref++;

            arch_syscall_return(curthread, dupfd);
            return;
        case F_GETFD:
            arch_syscall_return(curthread, file->fdflags);
            return;
        case F_SETFD:
            file->fdflags = arg & FD_CLOEXEC;
            arch_syscall_return(curthread, 0);
            return;
        case F_GETFL:
            arch_syscall_return(curthread, file->flags & ~FILE_SOCKET);
            return;
        case F_SETFL:
            file->flags = (file->flags & ~setfl) | ((int) arg & setfl);
            arch_syscall_return(curthread, 0);
            return;
    }

    arch_syscall_return(curthread, -EINVAL);
}

static void sys_chdir(const char *path)
{
    syscall_log(LOG_DEBUG, "chdir(path=%s)\n", path);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (!*path) {
        arch_syscall_return(curthread, -ENOENT);
        return;
    }

    int ret = 0;
    char *abs_path = NULL;

    struct vnode *vnode = NULL;
    ret = vfs_lookup(path, &PROC_UIO(curproc), &vnode, &abs_path);

    if (ret)
        goto free_resources;

    if (!vnode || !abs_path) {
        ret = vnode? -ENOMEM : -ENOENT;
        goto free_resources;
    }

    if (!S_ISDIR(vnode->mode)) {
        ret = -ENOTDIR;
        goto free_resources;
    }

    /* cwd is kept as a path, no vnode reference is held for it */
    char *cwd = strdup(abs_path);

    if (!cwd) {
        ret = -ENOMEM;
        goto free_resources;
    }

    kfree(curproc->cwd);
    curproc->cwd = cwd;

free_resources:
    if (vnode)
        vfs_close(vnode);

    kfree(abs_path);
    arch_syscall_return(curthread, ret);
}

static void sys_getcwd(char *buf, size_t size)
{
    syscall_log(LOG_DEBUG, "getcwd(buf=%p, size=%d)\n", buf, size);

    if (!size) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    size_t len = strlen(curproc->cwd);

    if (size < len + 1) {
        arch_syscall_return(curthread, -ERANGE);
        return;
    }

    if (uaccess_check(buf, len + 1)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    memcpy(buf, curproc->cwd, len + 1);
    arch_syscall_return(curthread, 0);
}

static void sys_thread_create(struct aquila_thread_args *__uthread)
{
    if (uaccess_check(__uthread, sizeof(struct aquila_thread_args))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    syscall_log(LOG_DEBUG,
            "thread_create(stack=%p, entry=%p, uentry=%p, arg=%p, attr=%p)\n",
            __uthread->stack, __uthread->entry, __uthread->uentry,
            __uthread->arg, __uthread->attr);

    uintptr_t stack = __uthread->stack;

    /* the kernel pushes the initial frame (3 words) on the user stack */
    if (stack < 3 * sizeof(uintptr_t) ||
        uaccess_check((void *) (stack - 3 * sizeof(uintptr_t)), 3 * sizeof(uintptr_t))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int err = 0;
    struct thread *thread = NULL;

    if ((err = thread_create(curthread, stack, __uthread->entry, __uthread->uentry, __uthread->arg, __uthread->attr, &thread))) {
        arch_syscall_return(curthread, err);
        return;
    }

    sched_thread_ready(thread);
    arch_syscall_return(curthread, thread->tid);
}

static void sys_thread_exit(void *value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_exit(value_ptr=%p)\n", value_ptr);

    //curthread->value_ptr = value_ptr;
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
    thread_queue_wakeup(&owner->thread_join);

    /* The thread stays in owner->threads as a zombie (for thread_join)
     * and is freed when the process exits. Its kernel stack (which we are
     * running on) is released from the idle stack by arch_cur_thread_kill */
    curthread->state = ZOMBIE;
    arch_cur_thread_kill();

    for (;;);
}

static void sys_thread_join(int tid, void **value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_join(tid=%d, value_ptr=%p)\n", tid, value_ptr);

    struct proc *owner = curproc;
    struct thread *thread = NULL;

    queue_for (node, &owner->threads) {
        struct thread *_thread = node->value;
        if (_thread->tid == tid)
            thread = _thread;
    }

    /* No such thread */
    if (!thread) {
        arch_syscall_return(curthread, -ECHILD);
        return;
    }

    if (thread->state == ZOMBIE) {  /* Thread is terminated */
        //*value_ptr = thread->value_ptr;
        arch_syscall_return(curthread, thread->tid);
        return;
    }

    while (thread->state != ZOMBIE) {
        if (thread_queue_sleep(&curproc->thread_join)) {
            arch_syscall_return(curthread, -EINTR);
            return;
        }
    }

    //*value_ptr = thread->value_ptr;
    arch_syscall_return(curthread, thread->tid);
}

static void sys_setpgid(pid_t pid, pid_t pgid)
{
    syscall_log(LOG_DEBUG, "setpgid(pid=%d, pgid=%d)\n", pid, pgid);

    if (pgid < 0) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    /* the caller or one of its children */
    struct proc *proc = pid ? proc_pid_find(pid) : curproc;

    if (!proc || (proc != curproc && proc->parent != curproc)) {
        arch_syscall_return(curthread, -ESRCH);
        return;
    }

    struct session *session = proc->pgrp->session;

    /* a session leader can't change its group, nor can a process outside
     * the caller's session */
    if (session != curproc->pgrp->session || session->leader == proc) {
        arch_syscall_return(curthread, -EPERM);
        return;
    }

    arch_syscall_return(curthread, proc_setpgid(proc, pgid ? pgid : proc->pid));
}

static void sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknod(path=%s, mode=%x, dev=%x)\n", path, mode, dev);
    arch_syscall_return(curthread, do_mknodat(AT_FDCWD, path, mode, dev));
}

static void sys_mknodat(int dirfd, const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknodat(dirfd=%d, path=%s, mode=%x, dev=%x)\n", dirfd, path, mode, dev);
    arch_syscall_return(curthread, do_mknodat(dirfd, path, mode, dev));
}

static void sys_lstat(const char *path, struct aquila_stat *buf)
{
    syscall_log(LOG_DEBUG, "lstat(path=%s, buf=%p)\n", path, buf);
    arch_syscall_return(curthread, do_fstatat(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW));
}

/*
 * There are no separate effective or saved ids, setuid() and setgid()
 * set the process ids: root may set any id, other users only their own.
 */
static void sys_setuid(uid_t uid)
{
    syscall_log(LOG_DEBUG, "setuid(uid=%d)\n", uid);

    if (curproc->uid != 0 && uid != curproc->uid) {
        arch_syscall_return(curthread, -EPERM);
        return;
    }

    curproc->uid = uid;
    arch_syscall_return(curthread, 0);
}

static void sys_ftruncate(int fildes, off_t len)
{
    syscall_log(LOG_DEBUG, "ftruncate(fildes=%d, len=%d)\n", fildes, len);

    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    /* must be open for writing */
    if (len < 0 || (file->flags & O_ACCMODE) == O_RDONLY) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    arch_syscall_return(curthread, vfs_file_trunc(file, len));
}

static void sys_setgid(gid_t gid)
{
    syscall_log(LOG_DEBUG, "setgid(gid=%d)\n", gid);

    if (curproc->uid != 0 && gid != curproc->gid) {
        arch_syscall_return(curthread, -EPERM);
        return;
    }

    curproc->gid = gid;
    arch_syscall_return(curthread, 0);
}

static void sys_getuid(void)
{
    syscall_log(LOG_DEBUG, "getuid()\n");
    arch_syscall_return(curthread, curproc->uid);
}

static void sys_getgid(void)
{
    syscall_log(LOG_DEBUG, "getgid()\n");
    arch_syscall_return(curthread, curproc->gid);
}

static void sys_mmap(struct aquila_mmap_args *args, void **ret)
{
    if (uaccess_check(args, sizeof(struct aquila_mmap_args)) || uaccess_check(ret, sizeof(void *))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    syscall_log(LOG_DEBUG, "mmap(addr=%p, len=%d, prot=%x, flags=%x, fildes=%d, off=%d, ret=%p)\n",
            args->addr, args->len, args->prot, args->flags, args->fildes, args->off, ret);

    int err = 0;

    struct file *file = fd_file(args->fildes);

    if (!file) {    /* Invalid File Descriptor */
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        arch_syscall_return(curthread, -ENODEV);
        return;
    }

    if (!args->len) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    /* a fixed mapping must lie entirely in user space */
    if ((args->flags & MAP_FIXED) && uaccess_check(args->addr, args->len)) {
        arch_syscall_return(curthread, -EINVAL);
        return;
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
    vm_entry->size   = args->len;
    vm_entry->flags  = args->prot & PROT_READ  ? VM_UR : 0;
    vm_entry->flags |= args->prot & PROT_WRITE ? VM_UW : 0;
    vm_entry->flags |= args->prot & PROT_EXEC  ? VM_UX : 0;
    vm_entry->flags |= args->flags & MAP_SHARED ? VM_SHARED : 0;
    vm_entry->off    = args->off;

    vm_entry->vm_object = vm_object_vnode(file->vnode);

    if (!(args->flags & MAP_FIXED))
        vm_entry->base = 0;  /* Allocate memory region */

    struct vm_space *vm_space = &curproc->vm_space;
    if ((err = vm_space_insert(vm_space, vm_entry)))
        goto error;

    if (!(args->flags & MAP_PRIVATE) && (err = vfs_map(vm_space, vm_entry)))
        goto error;

    *ret = (void *) vm_entry->base;

    arch_syscall_return(curthread, err);
    return;

error:
    if (vm_entry) {
        if (vm_entry->qnode)
            queue_node_remove(&curproc->vm_space.vm_entries, vm_entry->qnode);

        kfree(vm_entry);
    }

    arch_syscall_return(curthread, err);
    return;
}

static void sys_munmap(void *addr, size_t len)
{
    syscall_log(LOG_DEBUG, "munmap(addr=%p, len=%d)\n", addr, len);

    struct vm_space *vm_space = &curproc->vm_space;

    if (!len) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    queue_for (node, &vm_space->vm_entries) {
        struct vm_entry *vm_entry = node->value;

        /* heap and stack are managed by the kernel (sbrk/exec) */
        if (vm_entry == curproc->heap_vm || vm_entry == curproc->stack_vm)
            continue;

        if (vm_entry->base == (uintptr_t) addr && vm_entry->size == len) {
            queue_node_remove(&vm_space->vm_entries, vm_entry->qnode);
            vm_unmap_full(vm_space, vm_entry);
            kfree(vm_entry);
            arch_syscall_return(curthread, 0);
            return;
        }
    }

    /* Not found */
    arch_syscall_return(curthread, -EINVAL);
    return;
}

static void sys_socket(int domain, int type, int protocol)
{
    syscall_log(LOG_DEBUG, "socket(domain=%d, type=%d, protocol=%d)\n",
            domain, type, protocol);

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(curthread, -EMFILE);
        return;
    }
    
    int err = 0;
    struct file *file = &curproc->fds[fd];

    if ((err = socket_create(file, domain, type, protocol))) {
        proc_fd_release(curproc, fd);
        arch_syscall_return(curthread, err);
        return;
    }

    arch_syscall_return(curthread, fd);
    return;
}

static void sys_accept(int fd, const struct sockaddr *addr, uint32_t *len)
{
    syscall_log(LOG_DEBUG, "accept(fd=%d, addr=%p, len=%p)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    int conn_fd = proc_fd_get(curproc);

    if (conn_fd == -1) {
        arch_syscall_return(curthread, -EMFILE);
        return;
    }

    struct file *conn   = &curproc->fds[conn_fd];

    int err = 0;
    if ((err = socket_accept(socket, conn, addr, len))) {
        proc_fd_release(curproc, conn_fd);
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, conn_fd);
    return;
}

static void sys_bind(int fd, const struct sockaddr *addr, uint32_t len)
{
    syscall_log(LOG_DEBUG, "bind(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return; 
    }

    int err = 0;

    if ((err = socket_bind(file, addr, len))) {
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_connect(int fd, const struct sockaddr *addr, uint32_t len)
{
    syscall_log(LOG_DEBUG, "connect(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    struct file *socket = fd_file(fd);

    if (!socket) {
        arch_syscall_return(curthread, -EBADF);
        return; 
    }

    int err = 0;

    if ((err = socket_connect(socket, addr, len))) {
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_listen(int fd, int backlog)
{
    syscall_log(LOG_DEBUG, "listen(fd=%d, backlog=%d)\n", fd, backlog);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return; 
    }

    int err = 0;

    if ((err = socket_listen(file, backlog))) {
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_send(struct aquila_socket_io_args *s)
{
    if (uaccess_check(s, sizeof(struct aquila_socket_io_args))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "send(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if ((err = socket_send(file, buf, len, flags))) {
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_recv(struct aquila_socket_io_args *s)
{
    if (uaccess_check(s, sizeof(struct aquila_socket_io_args))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "recv(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    struct file *file = fd_file(fd);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return; 
    }

    int err = 0;

    if (uaccess_check(buf, len)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if ((err = socket_recv(file, buf, len, flags))) {
        arch_syscall_return(curthread, err);
        return; 
    }

    arch_syscall_return(curthread, 0);
    return;
}

static void sys_umask(mode_t mask)
{
    syscall_log(LOG_DEBUG, "umask(mask=%d)\n", mask);

    mode_t cur_mask = curproc->mask;
    curproc->mask = mask & 0777;

    arch_syscall_return(curthread, cur_mask);
    return;
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

static void sys_chmod(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "chmod(path=%s, mode=%o)\n", path, mode);
    arch_syscall_return(curthread, do_fchmodat(AT_FDCWD, path, mode, 0));
}

static void sys_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    syscall_log(LOG_DEBUG, "fchmodat(dirfd=%d, path=%s, mode=%o, flags=%x)\n", dirfd, path, mode, flags);
    arch_syscall_return(curthread, do_fchmodat(dirfd, path, mode, flags));
}

/* the vnode of an open file, NULL for sockets */
static struct vnode *fd_vnode(int fd, int *err)
{
    struct file *file = fd_file(fd);

    if (!file || (file->flags & FILE_SOCKET)) {
        *err = file ? -EINVAL : -EBADF;
        return NULL;
    }

    return file->vnode;
}

static void sys_fchmod(int fd, mode_t mode)
{
    syscall_log(LOG_DEBUG, "fchmod(fd=%d, mode=%o)\n", fd, mode);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chmod(vnode, mode, &PROC_UIO(curproc));

    arch_syscall_return(curthread, ret);
}

static void sys_sysconf(int name)
{
    syscall_log(LOG_DEBUG, "sysconf(name=%d)\n", name);
    arch_syscall_return(curthread, -ENOSYS);
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

static void sys_access(const char *path, int mode)
{
    syscall_log(LOG_DEBUG, "access(path=%s, mode=%d)\n", path, mode);
    arch_syscall_return(curthread, do_faccessat(AT_FDCWD, path, mode, 0));
}

static void sys_faccessat(int dirfd, const char *path, int mode, int flags)
{
    syscall_log(LOG_DEBUG, "faccessat(dirfd=%d, path=%s, mode=%d, flags=%x)\n", dirfd, path, mode, flags);
    arch_syscall_return(curthread, do_faccessat(dirfd, path, mode, flags));
}

static void sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    syscall_log(LOG_DEBUG, "gettimeofday(tv=%p, tz=%p)\n", tv, tz);

    if ((tv && uaccess_check(tv, sizeof(struct timeval))) ||
        (tz && uaccess_check(tz, sizeof(struct timezone)))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    arch_syscall_return(curthread, gettimeofday(tv, tz));
    return;
}

static void sys_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
    syscall_log(LOG_DEBUG, "sigmask(how=%d, set=%p, oldset=%p)\n", how, set, oldset);

    if ((set && uaccess_check(set, sizeof(sigset_t))) ||
        (oldset && uaccess_check(oldset, sizeof(sigset_t)))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    sigset_t mask = curproc->sigmask;

    if (set) {
        switch (how) {
            case SIG_BLOCK:   mask |= *set;  break;
            case SIG_UNBLOCK: mask &= ~*set; break;
            case SIG_SETMASK: mask = *set;   break;
            default:
                arch_syscall_return(curthread, -EINVAL);
                return;
        }
    }

    if (oldset)
        *oldset = curproc->sigmask;

    curproc->sigmask = mask & ~SIG_UNMASKABLE;
    arch_syscall_return(curthread, 0);

    /* a signal that is no longer blocked is delivered before returning */
    int signal = signal_next(curproc);
    if (signal)
        arch_handle_signal(signal);
}

static void sys_sigsuspend(const sigset_t *set)
{
    syscall_log(LOG_DEBUG, "sigsuspend(set=%p)\n", set);

    if (uaccess_check(set, sizeof(sigset_t))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    struct queue wait = {0};

    /* returns when a signal handler ran, with the old mask; a signal can
     * also be delivered when the thread is switched to (arch_thread_switch),
     * which restores the mask from sigsuspend_mask */
    arch_syscall_return(curthread, -EINTR);

    curthread->sigsuspend      = 1;
    curthread->sigsuspend_mask = curproc->sigmask;

    for (;;) {
        curproc->sigmask = *set & ~SIG_UNMASKABLE;

        int signal = signal_next(curproc);

        if (signal) {
            curproc->sigmask = curthread->sigsuspend_mask;
            curthread->sigsuspend = 0;
            arch_handle_signal(signal);

            /* the signal was ignored, keep waiting */
            curthread->sigsuspend = 1;
            continue;
        }

        /* signal_proc_send() wakes up the sleeping thread */
        thread_queue_sleep(&wait);
    }
}

static void sys_getppid(void)
{
    syscall_log(LOG_DEBUG, "getppid()\n");

    /* orphans are as if reparented to init */
    arch_syscall_return(curthread, curproc->parent ? curproc->parent->pid : 1);
}

typedef unsigned long fd_mask;

#define  FD_SETSIZE  64
#define   NFDBITS (sizeof (fd_mask) * 8)  /* bits per mask */
#define  _howmany(x,y)   (((x)+((y)-1))/(y))

typedef struct {
    fd_mask fds_bits[_howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

static void sys_select(struct aquila_select_args *args)
{
    syscall_log(LOG_DEBUG, "select(args=%p)\n", args);

    if (uaccess_check(args, sizeof(struct aquila_select_args))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int nfds = args->nfds;
    fd_set *readfds = args->readfds;
    fd_set *writefds = args->writefds;
    fd_set *exceptfds = args->exceptfds;
    struct timeval *timeout = args->timeout;

    if (nfds < 0) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    /* we can only look at descriptors that exist and fit in an fd_set */
    nfds = MIN(nfds, MIN(FDS_COUNT, FD_SETSIZE));

    if ((readfds && uaccess_check(readfds, sizeof(fd_set))) ||
        (writefds && uaccess_check(writefds, sizeof(fd_set)))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int count = 0;

    for (int i = 0; i < nfds; ++i) {
        if (readfds && readfds->fds_bits[i/NFDBITS] & (1 << (i % NFDBITS))) {
            struct file *file = fd_file(i);
            if (file && vfs_file_can_read(file, 1) > 0) {
                readfds->fds_bits[i/NFDBITS] |= (1 << (i % NFDBITS));
                ++count;
            } else {
                readfds->fds_bits[i/NFDBITS] &= ~(1 << (i % NFDBITS));
            }
        }

        if (writefds && writefds->fds_bits[i/NFDBITS] & (1 << (i % NFDBITS))) {
            struct file *file = fd_file(i);
            if (file && vfs_file_can_write(file, 1) > 0) {
                writefds->fds_bits[i/NFDBITS] |= (1 << (i % NFDBITS));
                ++count;
            } else {
                writefds->fds_bits[i/NFDBITS] &= ~(1 << (i % NFDBITS));
            }
        }
    }

    arch_syscall_return(curthread, count);
}

static void sys_getpgrp(void)
{
    syscall_log(LOG_DEBUG, "getpgrp()\n");
    arch_syscall_return(curthread, curproc->pgrp->pgid);
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

static void sys_chown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "chown(path=%s, owner=%d, group=%d)\n", path, owner, group);
    arch_syscall_return(curthread, do_fchownat(AT_FDCWD, path, owner, group, 0));
}

static void sys_fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
    syscall_log(LOG_DEBUG, "fchownat(dirfd=%d, path=%s, owner=%d, group=%d, flags=%x)\n",
            dirfd, path, owner, group, flags);
    arch_syscall_return(curthread, do_fchownat(dirfd, path, owner, group, flags));
}

static void sys_fchown(int fd, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "fchown(fd=%d, owner=%d, group=%d)\n", fd, owner, group);

    int ret = 0;
    struct vnode *vnode = fd_vnode(fd, &ret);

    if (vnode)
        ret = vfs_chown(vnode, owner, group, &PROC_UIO(curproc));

    arch_syscall_return(curthread, ret);
}

static void sys_lchown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "lchown(path=%s, owner=%d, group=%d)\n", path, owner, group);
    arch_syscall_return(curthread, do_fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW));
}

/* `path' NULL: the file `dirfd' refers to (futimens) */
static void sys_utimensat(int dirfd, const char *path, const struct aquila_timespec times[2], int flags)
{
    syscall_log(LOG_DEBUG, "utimensat(dirfd=%d, path=%s, times=%p, flags=%x)\n", dirfd, path, times, flags);

    struct vnode *vnode = NULL;
    struct uio uio = PROC_UIO(curproc);
    int ret = 0;

    if (flags & ~AT_SYMLINK_NOFOLLOW) {
        ret = -EINVAL;
        goto done;
    }

    if (times && uaccess_check(times, 2 * sizeof(struct aquila_timespec))) {
        ret = -EFAULT;
        goto done;
    }

    if (!path) {
        if ((vnode = fd_vnode(dirfd, &ret)))
            ret = vfs_utimens(vnode, times, &uio);

        goto done;
    }

    if ((ret = at_lookup(dirfd, path, flags, &uio, &vnode)))
        goto done;

    ret = vfs_utimens(vnode, times, &uio);
    vfs_close(vnode);

done:
    arch_syscall_return(curthread, ret);
}

static void sys_rmdir(const char *path)
{
    syscall_log(LOG_DEBUG, "rmdir(path=%s)\n", path);
    arch_syscall_return(curthread, do_unlinkat(AT_FDCWD, path, AT_REMOVEDIR));
}

static void sys_symlinkat(const char *target, int dirfd, const char *path)
{
    syscall_log(LOG_DEBUG, "symlinkat(target=%s, dirfd=%d, path=%s)\n", target, dirfd, path);

    struct uio uio;
    int ret;

    if (uaccess_str_check(target) || uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (!(ret = at_uio(dirfd, path, &uio)))
        ret = vfs_symlink(target, path, &uio);

    arch_syscall_return(curthread, ret);
}

static void sys_readlinkat(int dirfd, const char *path, char *buf, size_t size)
{
    syscall_log(LOG_DEBUG, "readlinkat(dirfd=%d, path=%s, buf=%p, size=%d)\n", dirfd, path, buf, size);

    struct uio uio;
    int ret;

    if (uaccess_str_check(path) || uaccess_check(buf, size)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (!(ret = at_uio(dirfd, path, &uio)))
        ret = vfs_readlink(path, &uio, buf, size);

    arch_syscall_return(curthread, ret);
}

static void sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    syscall_log(LOG_DEBUG, "renameat(olddirfd=%d, oldpath=%s, newdirfd=%d, newpath=%s)\n",
            olddirfd, oldpath, newdirfd, newpath);

    char *oabs = NULL, *nabs = NULL;
    int ret;

    if (!(ret = at_path(olddirfd, oldpath, &oabs)) &&
        !(ret = at_path(newdirfd, newpath, &nabs)))
        ret = vfs_rename(oabs, nabs, &PROC_UIO(curproc));

    kfree(oabs);
    kfree(nabs);
    arch_syscall_return(curthread, ret);
}

static void sys_fchdir(int fd)
{
    syscall_log(LOG_DEBUG, "fchdir(fd=%d)\n", fd);

    struct file *file = fd_file(fd);
    int ret = 0;

    if (!file) {
        ret = -EBADF;
    } else if ((file->flags & FILE_SOCKET) || !S_ISDIR(file->vnode->mode) || !file->path) {
        ret = -ENOTDIR;
    } else if (vfs_access(file->vnode, &PROC_UIO(curproc), VFS_X)) {
        ret = -EACCES;
    } else {
        char *cwd = strdup(file->path);

        if (cwd) {
            kfree(curproc->cwd);
            curproc->cwd = cwd;
        } else {
            ret = -ENOMEM;
        }
    }

    arch_syscall_return(curthread, ret);
}

static void sys_setsid(void)
{
    syscall_log(LOG_DEBUG, "setsid()\n");

    struct proc *proc = curproc;
    struct pgroup *pgrp = proc->pgrp;
    struct qnode *pgrp_node = proc->pgrp_node;

    /* a group leader can't leave its group, the group would be in two sessions */
    if (pgrp->pgid == proc->pid) {
        arch_syscall_return(curthread, -EPERM);
        return;
    }

    int ret = session_new(proc);

    if (ret) {
        proc->pgrp = pgrp;
        proc->pgrp_node = pgrp_node;
        arch_syscall_return(curthread, ret);
        return;
    }

    queue_node_remove(pgrp->procs, pgrp_node);
    pgrp_release(pgrp);
    arch_syscall_return(curthread, proc->pid);
}

static void sys_nanosleep(const struct aquila_timespec *req, struct aquila_timespec *rem)
{
    syscall_log(LOG_DEBUG, "nanosleep(req=%p, rem=%p)\n", req, rem);

    if (uaccess_check(req, sizeof(struct aquila_timespec)) ||
        (rem && uaccess_check(rem, sizeof(struct aquila_timespec)))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (req->tv_nsec >= 1000000000) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    uint64_t ns = req->tv_sec * 1000000000ULL + req->tv_nsec;

    curthread->sleep_left = rem;
    sched_sleep(arch_rtime_ns() + ns);
    arch_syscall_return(curthread, 0);
}

static void sys_clock_gettime(int clock, struct aquila_timespec *ts)
{
    syscall_log(LOG_DEBUG, "clock_gettime(clock=%d, ts=%p)\n", clock, ts);

    if (uaccess_check(ts, sizeof(struct aquila_timespec))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    struct timespec now;
    uint64_t ns;

    switch (clock) {
        case CLOCK_REALTIME:
            gettime(&now);
            ts->tv_sec  = now.tv_sec;
            ts->tv_nsec = now.tv_nsec;
            break;
        case CLOCK_MONOTONIC:
            ns = arch_rtime_ns();
            ts->tv_sec  = ns / 1000000000ULL;
            ts->tv_nsec = ns % 1000000000ULL;
            break;
        default:
            arch_syscall_return(curthread, -EINVAL);
            return;
    }

    arch_syscall_return(curthread, 0);
}

static void sys_sethostname(const char *name, size_t len)
{
    syscall_log(LOG_DEBUG, "sethostname(name=%p, len=%d)\n", name, len);

    if (curproc->uid != 0) {
        arch_syscall_return(curthread, -EPERM);
        return;
    }

    if (len >= sizeof(nodename)) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    if (uaccess_check(name, len)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    memcpy(nodename, name, len);
    nodename[len] = '\0';
    arch_syscall_return(curthread, 0);
}

void (*syscall_table[])() = {
    [SYS_exit]          = sys_exit,
    [SYS_close]         = sys_close,
    [SYS_execve]        = sys_execve,
    [SYS_fork]          = sys_fork,
    [SYS_fstat]         = sys_fstat,
    [SYS_getpid]        = sys_getpid,
    [SYS_isatty]        = sys_isatty,
    [SYS_kill]          = sys_kill,
    [SYS_link]          = sys_link,
    [SYS_lseek]         = sys_lseek,
    [SYS_open]          = sys_open,
    [SYS_read]          = sys_read,
    [SYS_sbrk]          = sys_sbrk,
    [SYS_stat]          = sys_stat,
    [SYS_times]         = sys_times,
    [SYS_unlink]        = sys_unlink,
    [SYS_waitpid]       = sys_waitpid,
    [SYS_write]         = sys_write,
    [SYS_ioctl]         = sys_ioctl,
    [SYS_sigaction]     = sys_sigaction,
    [SYS_readdir]       = sys_readdir,
    [SYS_mount]         = sys_mount,
    [SYS_mkdir]         = sys_mkdir,
    [SYS_uname]         = sys_uname,
    [SYS_pipe]          = sys_pipe,
    [SYS_fcntl]         = sys_fcntl,
    [SYS_chdir]         = sys_chdir,
    [SYS_getcwd]        = sys_getcwd,
    [SYS_thread_create] = sys_thread_create,
    [SYS_thread_exit]   = sys_thread_exit,
    [SYS_thread_join]   = sys_thread_join,
    [SYS_setpgid]       = sys_setpgid,
    [SYS_mknod]         = sys_mknod,
    [SYS_lstat]         = sys_lstat,
    [SYS_getuid]        = sys_getuid,
    [SYS_getgid]        = sys_getgid,
    [SYS_mmap]          = sys_mmap,
    [SYS_munmap]        = sys_munmap,
    [SYS_socket]        = sys_socket,
    [SYS_accept]        = sys_accept,
    [SYS_bind]          = sys_bind,
    [SYS_connect]       = sys_connect,
    [SYS_listen]        = sys_listen,
    [SYS_send]          = sys_send,
    [SYS_recv]          = sys_recv,
    [SYS_umask]         = sys_umask,
    [SYS_chmod]         = sys_chmod,
    [SYS_sysconf]       = sys_sysconf,
    [SYS_gettimeofday]  = sys_gettimeofday,
    [SYS_access]        = sys_access,
    [SYS_sigmask]       = sys_sigmask,
    [SYS_select]        = sys_select,
    [SYS_getpgrp]       = sys_getpgrp,
    [SYS_chown]         = sys_chown,
    [SYS_fchown]        = sys_fchown,
    [SYS_lchown]        = sys_lchown,
    [SYS_rmdir]         = sys_rmdir,
    [SYS_setuid]        = sys_setuid,
    [SYS_setgid]        = sys_setgid,
    [SYS_ftruncate]     = sys_ftruncate,
    [SYS_sigsuspend]    = sys_sigsuspend,
    [SYS_getppid]       = sys_getppid,
    [SYS_openat]        = sys_openat,
    [SYS_fstatat]       = sys_fstatat,
    [SYS_unlinkat]      = sys_unlinkat,
    [SYS_mknodat]       = sys_mknodat,
    [SYS_linkat]        = sys_linkat,
    [SYS_symlinkat]     = sys_symlinkat,
    [SYS_readlinkat]    = sys_readlinkat,
    [SYS_renameat]      = sys_renameat,
    [SYS_faccessat]     = sys_faccessat,
    [SYS_fchmodat]      = sys_fchmodat,
    [SYS_fchownat]      = sys_fchownat,
    [SYS_utimensat]     = sys_utimensat,
    [SYS_fchmod]        = sys_fchmod,
    [SYS_fchdir]        = sys_fchdir,
    [SYS_setsid]        = sys_setsid,
    [SYS_nanosleep]     = sys_nanosleep,
    [SYS_clock_gettime] = sys_clock_gettime,
    [SYS_sethostname]   = sys_sethostname,
};

const size_t syscall_cnt = sizeof(syscall_table)/sizeof(syscall_table[0]);
