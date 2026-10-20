/**********************************************************************
 *                          System Calls
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */


#include <core/system.h>
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

#include <bits/errno.h>
#include <bits/dirent.h>
#include <bits/utsname.h>
#include <bits/fcntl.h>
#include <bits/mman.h>

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

static void sys_fstat(int fildes, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "fstat(fildes=%d, buf=%p)\n", fildes, buf);

    struct file *file = fd_file(fildes);

    if (!file) {
        arch_syscall_return(curthread, -EBADF);
        return;
    }

    if (uaccess_check(buf, sizeof(struct stat))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if (file->flags & FILE_SOCKET) {
        /* sockets have no vnode, report a minimal stat */
        memset(buf, 0, sizeof(struct stat));
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

    if (file->flags & FILE_SOCKET) {
        arch_syscall_return(curthread, -ENOTTY);
        return;
    }

    //arch_syscall_return(curthread, node->rdev & (136 << 8));
    arch_syscall_return(curthread, 1);
}

static void sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);
    int ret = signal_send(pid, sig);
    arch_syscall_return(curthread, ret);
}

static void sys_link(const char *oldpath, const char *newpath)
{
    syscall_log(LOG_DEBUG, "link(oldpath=%s, newpath=%s)\n",
            oldpath, newpath);

    /* TODO */

    arch_syscall_return(curthread, -ENOSYS);
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

static void sys_open(const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "open(path=%s, oflags=0x%x, mode=0x%x)\n", path, oflags, mode);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int fd = proc_fd_get(curproc);  /* Find a free file descriptor */

    if (fd == -1) {
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(curthread, -EMFILE);
        return;
    }

    /* Look up the file */
    struct vnode *vnode = NULL;
    struct uio uio = PROC_UIO(curproc);
    uio.flags = oflags;

    int ret = vfs_lookup(path, &uio, &vnode, NULL);

    if (ret) {   /* Lookup failed */
        if ((ret == -ENOENT) && (oflags & O_CREAT)) {
            if ((ret = vfs_creat(path, mode, &uio, &vnode))) {
                goto done;
            }

            goto o_creat;
        }

        goto done;
    }

    if (ret)
        goto done;

o_creat:
    curproc->fds[fd] = (struct file) {
        .vnode  = vnode,
        .offset = 0,
        .flags  = oflags,
    };

    if ((ret = vfs_perms_check(&curproc->fds[fd], &uio)))
        goto done;

    ret = vfs_file_open(&curproc->fds[fd]);

done:
    if (ret < 0) { /* open returned an error code */
        proc_fd_release(curproc, fd);
        vfs_close(vnode);
    } else {
        ret = fd;
    }

    arch_syscall_return(curthread, ret);
    return;
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

static void sys_stat(const char *path, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "stat(path=%s, buf=%p)\n", path, buf);

    struct vnode *vnode = NULL;
    int ret = 0;
    struct uio uio = PROC_UIO(curproc);

    if (uaccess_str_check(path) || uaccess_check(buf, sizeof(struct stat))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if ((ret = vfs_lookup(path, &uio, &vnode, NULL))) {
        arch_syscall_return(curthread, ret);
        return;
    }

    ret = vfs_stat(vnode, buf);
    vfs_close(vnode);
    arch_syscall_return(curthread, ret);
}

static void sys_times()
{

}

static void sys_unlink(const char *path)
{
    syscall_log(LOG_DEBUG, "unlink(path=%p)\n", path);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    struct uio uio = PROC_UIO(curproc);
    int ret = vfs_unlink(path, &uio);
    arch_syscall_return(curthread, ret);
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
    return;
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

static void sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    syscall_log(LOG_DEBUG, "sigaction(sig=%d, act=%p, oact=%p)\n",
            sig, act, oact);

    if (sig < 1 || sig > SIG_MAX) {
        arch_syscall_return(curthread, -EINVAL);
        return;
    }

    if ((act && uaccess_check(act, sizeof(struct sigaction))) ||
        (oact && uaccess_check(oact, sizeof(struct sigaction)))) {
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
        memcpy(oact, &curproc->sigaction[sig], sizeof(struct sigaction));

    if (act)
        memcpy(&curproc->sigaction[sig], act, sizeof(struct sigaction));

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
    arch_syscall_return(curthread, ret);
    return;
}

struct mount_struct {
    const char *type;
    const char *dir;
    int flags;
    void *data;
} __packed;

static void sys_mount(struct mount_struct *args)
{
    if (curproc->uid != 0) {
        arch_syscall_return(curthread, -EACCES);
        return;
    }

    if (uaccess_check(args, sizeof(struct mount_struct))) {
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

static void sys_mkdir(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "mkdir(path=%s, mode=%x)\n", path, mode);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    struct uio uio = PROC_UIO(curproc);

    int ret = vfs_mkdir(path, mode, &uio, NULL);
    arch_syscall_return(curthread, ret);
    return;
}

static void sys_uname(struct utsname *name)
{
    syscall_log(LOG_DEBUG, "uname(name=%p)\n", name);

    if (uaccess_check(name, sizeof(struct utsname))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    strcpy(name->sysname,  UTSNAME_SYSNAME);
    strcpy(name->nodename, UTSNAME_NODENAME);
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

    int dupfd = 0;

    switch (cmd) {
        case F_DUPFD:
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

            /* the new descriptor holds its own reference (as in fork) */
            if (file->flags & FILE_SOCKET)
                file->socket->ref++;
            else
                file->vnode->ref++;

            arch_syscall_return(curthread, dupfd);
            return;
        case F_GETFD:
            arch_syscall_return(curthread, file->flags);
            return;
        case F_SETFD:
            /* never let user space toggle the socket marker, it decides
             * whether the vnode/socket union is a vnode or a socket */
            file->flags = (file->flags & FILE_SOCKET) | ((int) arg & ~FILE_SOCKET); /* XXX */
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

struct __uthread {
    uintptr_t stack;
    uintptr_t entry;    /* Sys entry */
    uintptr_t uentry;   /* User entry */
    uintptr_t arg;
    uintptr_t attr;
};

static void sys_thread_create(struct __uthread *__uthread)
{
    if (uaccess_check(__uthread, sizeof(struct __uthread))) {
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

    //if (pid == 0 && pgid == 0) {
        int ret = pgrp_new(curproc, NULL);
        arch_syscall_return(curthread, ret);
    //} else {
    //    panic("Unsupported");
    //}
}

static void sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknod(path=%s, mode=%x, dev=%x)\n", path, mode, dev);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int ret = vfs_mknod(path, mode, dev, &PROC_UIO(curproc), NULL);
    arch_syscall_return(curthread, ret);
}

static void sys_lstat(const char *path, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "lstat(path=%s, buf=%p)\n", path, buf);

    struct vnode *vnode = NULL;
    int ret = 0;
    struct uio uio = PROC_UIO(curproc);
    uio.flags = O_NOFOLLOW;

    if (uaccess_str_check(path) || uaccess_check(buf, sizeof(struct stat))) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    if ((ret = vfs_lookup(path, &uio, &vnode, NULL))) {
        arch_syscall_return(curthread, ret);
        return;
    }

    ret = vfs_stat(vnode, buf);
    vfs_close(vnode);
    arch_syscall_return(curthread, ret);
}

static void sys_auth(uint32_t uid, const char *pw)
{
    syscall_log(LOG_DEBUG, "auth(uid=%d, pw=%s)\n", uid, pw);

    curproc->uid = uid;   /* XXX */
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

struct mmap_args {
    void    *addr;
    size_t  len;
    int     prot;
    int     flags;
    int     fildes;
    off_t   off;
} __packed;

static void sys_mmap(struct mmap_args *args, void **ret)
{
    if (uaccess_check(args, sizeof(struct mmap_args)) || uaccess_check(ret, sizeof(void *))) {
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

struct socket_io_syscall {
    int fd;
    void *buf;
    size_t len;
    int flags;
} __attribute__((packed));

static void sys_send(struct socket_io_syscall *s)
{
    if (uaccess_check(s, sizeof(struct socket_io_syscall))) {
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

static void sys_recv(struct socket_io_syscall *s)
{
    if (uaccess_check(s, sizeof(struct socket_io_syscall))) {
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

static void sys_chmod(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "chmod(path=%s, mode=%d)\n", path, mode);
    arch_syscall_return(curthread, -ENOSYS);
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

static void sys_access(const char *path, int mode)
{
    syscall_log(LOG_DEBUG, "access(path=%s, mode=%d)\n", path, mode);

    if (uaccess_str_check(path)) {
        arch_syscall_return(curthread, -EFAULT);
        return;
    }

    int err = 0;

    /* Look up the file */
    struct vnode *vnode = NULL;
    struct file  file;

    struct uio uio = PROC_UIO(curproc);

    uio.flags |= mode & R_OK? O_RDONLY : 0;
    uio.flags |= mode & W_OK? O_WRONLY : 0;
    uio.flags |= mode & X_OK? O_EXEC   : 0;

    err = vfs_lookup(path, &uio, &vnode, NULL);

    if (err) goto done;

    file = (struct file) {
        .vnode  = vnode,
        .offset = 0,
        .flags  = uio.flags,
    };

    if ((err = vfs_perms_check(&file, &uio)))
        goto done;

done:
    if (vnode)
        vfs_close(vnode);

    arch_syscall_return(curthread, err);

    return;
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

static void sys_sigmask(int how, void *set, void *oldset)
{
    syscall_log(LOG_DEBUG, "sigmask(how=%d, set=%p, oldset=%p)\n", how, set, oldset);
    arch_syscall_return(curthread, -ENOTSUP);
}

typedef unsigned long fd_mask;

#define  FD_SETSIZE  64
#define   NFDBITS (sizeof (fd_mask) * 8)  /* bits per mask */
#define  _howmany(x,y)   (((x)+((y)-1))/(y))

typedef struct {
    fd_mask fds_bits[_howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

struct select_args {
    int nfds;
    fd_set *readfds;
    fd_set *writefds;
    fd_set *exceptfds;
    struct timeval *timeout;
};

static void sys_select(struct select_args *args)
{
    syscall_log(LOG_DEBUG, "select(args=%p)\n", args);

    if (uaccess_check(args, sizeof(struct select_args))) {
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

static void sys_chown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "chown(path=%s, ownder=%d, group=%d)\n", path, owner, group);
    arch_syscall_return(curthread, -ENOSYS);
}

static void sys_fchown(int fd, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "fchown(fd=%d, owner=%d, group=%d)\n", fd, owner, group);
    arch_syscall_return(curthread, -ENOSYS);
}

static void sys_lchown(const char *path, uid_t owner, gid_t group)
{
    syscall_log(LOG_DEBUG, "lchown(path=%s, owner=%d, group=%d)\n", path, owner, group);
    arch_syscall_return(curthread, -ENOSYS);
}

static void sys_utime(const char *path, const struct utimbuf *times)
{
    syscall_log(LOG_DEBUG, "utime(path=%s, times=%p)\n", path, times);
    arch_syscall_return(curthread, -ENOSYS);
}

static void sys_rmdir(const char *path)
{
    syscall_log(LOG_DEBUG, "rmdir(path=%s)\n", path);
    arch_syscall_return(curthread, -ENOSYS);
}

void (*syscall_table[])() =  {
    /* 00 */    NULL,
    /* 01 */    sys_exit,
    /* 02 */    sys_close,
    /* 03 */    sys_execve,
    /* 04 */    sys_fork,
    /* 05 */    sys_fstat,
    /* 06 */    sys_getpid,
    /* 07 */    sys_isatty,
    /* 08 */    sys_kill,
    /* 09 */    sys_link,
    /* 10 */    sys_lseek,
    /* 11 */    sys_open,
    /* 12 */    sys_read,
    /* 13 */    sys_sbrk,
    /* 14 */    sys_stat,
    /* 15 */    sys_times,
    /* 16 */    sys_unlink,
    /* 17 */    sys_waitpid,
    /* 18 */    sys_write,
    /* 19 */    sys_ioctl,
    /* 20 */    sys_sigaction,
    /* 21 */    sys_readdir,
    /* 22 */    sys_mount,
    /* 23 */    sys_mkdir,
    /* 24 */    sys_uname,
    /* 25 */    sys_pipe,
    /* 26 */    sys_fcntl,
    /* 27 */    sys_chdir,
    /* 28 */    sys_getcwd,
    /* 29 */    sys_thread_create,
    /* 30 */    sys_thread_exit,
    /* 31 */    sys_thread_join,
    /* 32 */    sys_setpgid,
    /* 33 */    sys_mknod,
    /* 34 */    sys_lstat,
    /* 35 */    sys_auth, /* deprecated */
    /* 36 */    sys_getuid,
    /* 37 */    sys_getgid,
    /* 38 */    sys_mmap,
    /* 39 */    sys_munmap,
    /* 40 */    sys_socket,
    /* 41 */    sys_accept,
    /* 42 */    sys_bind,
    /* 43 */    sys_connect,
    /* 44 */    sys_listen,
    /* 45 */    sys_send,
    /* 46 */    sys_recv,
    /* 47 */    sys_umask,
    /* 48 */    sys_chmod,
    /* 49 */    sys_sysconf,
    /* 50 */    sys_gettimeofday,
    /* 51 */    sys_access,
    /* 52 */    sys_sigmask,
    /* 53 */    sys_select,
    /* 54 */    sys_getpgrp,
    /* 55 */    sys_chown,
    /* 56 */    sys_fchown,
    /* 57 */    sys_lchown,
    /* 58 */    sys_utime,
    /* 59 */    sys_rmdir,
};

const size_t syscall_cnt = sizeof(syscall_table)/sizeof(syscall_table[0]);
