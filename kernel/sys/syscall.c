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
static inline int syscall_log(int level, const char *fmt, ...)
{
    if (level <= syscall_log_level) {
        printk("syscall: [%d:%d] %s: ", cur_thread->owner->pid,
                cur_thread->tid, cur_thread->owner->name);
        va_list args;
        va_start(args, fmt);
        vprintk(fmt, args);
        va_end(args);
    }

    return 0;
}

static void sys_exit(int code)
{
    syscall_log(LOG_DEBUG, "exit(code=%d)\n", code);

    struct proc *owner = cur_thread->owner;

    owner->exit = PROC_EXIT(code, 0);  /* Child exited normally */

    proc_kill(owner);
    arch_sleep();

    /* We should never reach this anyway */
    for (;;);
}

static void sys_close(int fildes)
{
    syscall_log(LOG_DEBUG, "close(fildes=%d)\n", fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];
    
    if (file == (void *) -1 || !file->inode) {
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    int ret = vfs_file_close(file);
    cur_thread->owner->fds[fildes].inode = NULL;
    arch_syscall_return(cur_thread, ret);
}

static void sys_execve(const char *path, char * const argp[], char * const envp[])
{
    syscall_log(LOG_DEBUG, "execve(path=%s, argp=%p, envp=%p)\n", path, argp, envp);

    if (!path || !*path) {
        arch_syscall_return(cur_thread, -ENOENT);
        return;
    }

    int err = 0;
    char *fn = strdup(path);

    if (!fn) {
        arch_syscall_return(cur_thread, -ENOMEM);
        return;
    }

    err = proc_execve(cur_thread, fn, argp, envp);

    kfree(fn);

    if (err) {
        arch_syscall_return(cur_thread, err);
    } else {
        sched_thread_spawn(cur_thread);
    }
}

static void sys_fork(void)
{
    syscall_log(LOG_DEBUG, "fork()\n");

    struct proc *fork = NULL;
    proc_fork(cur_thread, &fork);

    /* Returns are handled inside proc_fork */
    if (fork != NULL) {
        struct thread *thread = (struct thread *) fork->threads.head->value;
        sched_thread_ready(thread);
    }
}

static void sys_fstat(int fildes, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "fstat(fildes=%d, buf=%p)\n", fildes, buf);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];
    
    if (!file->inode) {
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    struct inode *inode = file->inode;

    int ret = vfs_stat(inode, buf);
    arch_syscall_return(cur_thread, ret);
}

static void sys_getpid(void)
{
    syscall_log(LOG_DEBUG, "getpid()\n");
    arch_syscall_return(cur_thread, cur_thread->owner->pid);
}

static void sys_isatty(int fildes)
{
    syscall_log(LOG_DEBUG, "isatty(fildes=%d)\n", fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct inode *node = cur_thread->owner->fds[fildes].inode;

    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    //arch_syscall_return(cur_thread, node->rdev & (136 << 8));
    arch_syscall_return(cur_thread, 1);
}

static void sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);
    int ret = signal_send(pid, sig);
    arch_syscall_return(cur_thread, ret);
}

static void sys_link(const char *oldpath, const char *newpath)
{
    syscall_log(LOG_DEBUG, "link(oldpath=%s, newpath=%s)\n",
            oldpath, newpath);

    /* TODO */

    arch_syscall_return(cur_thread, -ENOSYS);
}

static void sys_lseek(int fildes, off_t offset, int whence)
{
    syscall_log(LOG_DEBUG, "lseek(fildes=%d, offset=%d, whence=%d)\n",
            fildes, offset, whence);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];

    if (!file->inode) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    int ret = vfs_file_lseek(file, offset, whence);
    arch_syscall_return(cur_thread, ret);
}

static void sys_open(const char *path, int oflags, mode_t mode)
{
    syscall_log(LOG_DEBUG, "open(path=%s, oflags=0x%x, mode=0x%x)\n", path, oflags, mode);
    
    int fd = proc_fd_get(cur_thread->owner);  /* Find a free file descriptor */

    if (fd == -1) {
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(cur_thread, -EMFILE);
        return;
    }

    /* Look up the file */
    struct vnode vnode;
    struct inode *inode = NULL;
    struct uio uio = PROC_UIO(cur_thread->owner);
    uio.flags = oflags;

    int ret = vfs_lookup(path, &uio, &vnode, NULL);

    if (ret) {   /* Lookup failed */
        if ((ret == -ENOENT) && (oflags & O_CREAT)) {
            if ((ret = vfs_creat(path, mode, &uio, &inode))) {
                goto done;
            }

            goto o_creat;
        }

        goto done;
    }

    ret = vfs_vget(&vnode, &inode);

    if (ret)
        goto done;

o_creat:
    cur_thread->owner->fds[fd] = (struct file) {
        .inode  = inode,
        .offset = 0,
        .flags  = oflags,
    };

    if ((ret = vfs_perms_check(&cur_thread->owner->fds[fd], &uio)))
        goto done;

    ret = vfs_file_open(&cur_thread->owner->fds[fd]);

done:
    if (ret < 0) { /* open returned an error code */
        proc_fd_release(cur_thread->owner, fd);
        vfs_close(inode);
    } else {
        ret = fd;
    }

    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_read(int fildes, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "read(fd=%d, buf=%p, count=%d)\n", fildes, buf, nbytes);
    
    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];
    int ret = vfs_file_read(file, buf, nbytes);
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_sbrk(ptrdiff_t incr)
{
    syscall_log(LOG_DEBUG, "sbrk(incr=0x%x)\n", incr);

    uintptr_t ret = cur_thread->owner->heap;
    cur_thread->owner->heap += incr;
    cur_thread->owner->heap_vm->size += incr;

    syscall_log(LOG_DEBUG, "current heap %p\n", ret);
    arch_syscall_return(cur_thread, ret);

    return;
}

static void sys_stat(const char *path, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "stat(path=%s, buf=%p)\n", path, buf);

    struct vnode vnode;
    struct inode *inode = NULL;
    int ret = 0;
    struct uio uio = PROC_UIO(cur_thread->owner);

    if ((ret = vfs_lookup(path, &uio, &vnode, NULL))) {
        arch_syscall_return(cur_thread, ret);
        return;
    }

    if ((ret = vfs_vget(&vnode, &inode))) {
        arch_syscall_return(cur_thread, ret);
        return;
    }

    ret = vfs_stat(inode, buf);
    arch_syscall_return(cur_thread, ret);
}

static void sys_times()
{

}

static void sys_unlink(const char *path)
{
    syscall_log(LOG_DEBUG, "unlink(path=%p)\n", path);

    struct uio uio = PROC_UIO(cur_thread->owner);
    int ret = vfs_unlink(path, &uio);
    arch_syscall_return(cur_thread, ret);
}

#define WNOHANG 1
static void sys_waitpid(int pid, int *stat_loc, int options)
{
    syscall_log(LOG_DEBUG, "waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", pid, stat_loc, options);

    int nohang = options & WNOHANG;

    if (pid < -1) {
        /* wait for any child process whose process group ID
              is equal to the absolute value of pid */
        panic("unsupported");
    } else if (pid == -1) {

        /* wait for any child process */
        for (;;) {
            int found = 0;

            for (struct qnode *node = procs->head; node; node = node->next) {
                struct proc *proc = node->value;

                if (proc->parent != cur_thread->owner)
                    continue;

                found = 1;

                if (!proc->running) {
                    if (stat_loc)
                        *stat_loc = proc->exit;

                    arch_syscall_return(cur_thread, proc->pid);
                    proc_reap(proc);
                    return;
                }
            }

            if (nohang) {
                arch_syscall_return(cur_thread, found? 0 : -ECHILD);
                return;
            }

            if (thread_queue_sleep(&cur_thread->owner->wait_queue)) {
                arch_syscall_return(cur_thread, -EINTR);
                return;
            }
        }

    } else if (pid == 0) {
        /* wait for any child process whose process group ID
              is equal to that of the calling process */
        panic("unsupported");
    } else {
        /* wait for the child whose process ID is equal to the
              value of pid */
        struct proc *child = proc_pid_find(pid);

        /* If pid is invalid or current process is not parent of child */
        if (child == NULL || child->parent != cur_thread->owner) {
            arch_syscall_return(cur_thread, -ECHILD);
            return;
        }

        if (!(child->running)) {  /* Child is killed */
            *stat_loc = child->exit;
            arch_syscall_return(cur_thread, child->pid);
            proc_reap(child);
            return;
        }

        /*
        if (options & WNOHANG) {
            arch_syscall_return(cur_thread, 0);
            return;
        }
        */

        while (child->running) {
            if (thread_queue_sleep(&cur_thread->owner->wait_queue)) {
                arch_syscall_return(cur_thread, -EINTR);
                return;
            }
        }

        *stat_loc = child->exit;
        arch_syscall_return(cur_thread, child->pid);
        proc_reap(child);
    }
}

static void sys_write(int fd, void *buf, size_t nbytes)
{
    syscall_log(LOG_DEBUG, "write(fd=%d, buf=%p, nbytes=%d)\n", fd, buf, nbytes);
    
    if (fd < 0 || fd >= FDS_COUNT) {   /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];
    int ret = vfs_file_write(file, buf, nbytes);
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_ioctl(int fd, int request, void *argp)
{
    syscall_log(LOG_DEBUG, "ioctl(fd=%d, request=0x%x, argp=%p)\n",
            fd, request, argp);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];
    int ret = vfs_file_ioctl(file, request, argp);
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    syscall_log(LOG_DEBUG, "sigaction(sig=%d, act=%p, oact=%p)\n",
            sig, act, oact);

    if (sig < 1 || sig > SIG_MAX) {
        arch_syscall_return(cur_thread, -EINVAL);
        return;
    }

    if (oact)
        memcpy(oact, &cur_thread->owner->sigaction[sig], sizeof(struct sigaction));

    if (act)
        memcpy(&cur_thread->owner->sigaction[sig], act, sizeof(struct sigaction));

    arch_syscall_return(cur_thread, 0);
}

static void sys_readdir(int fd, struct dirent *dirent)
{
    syscall_log(LOG_DEBUG, "readdir(fd=%d, dirent=%p)\n", fd, dirent);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];
    int ret = vfs_file_readdir(file, dirent);
    arch_syscall_return(cur_thread, ret);
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
    if (cur_thread->owner->uid != 0) {
        arch_syscall_return(cur_thread, -EACCES);
        return;
    }

    const char *type = args->type;
    const char *dir  = args->dir;
    int flags = args->flags;
    void *data = args->data;

    syscall_log(LOG_DEBUG, "mount(type=%s, dir=%s, flags=%x, data=%p)\n",
            type, dir, flags, data);

    int ret = vfs_mount(type, dir, flags, data, &PROC_UIO(cur_thread->owner));
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_mkdir(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "mkdir(path=%s, mode=%x)\n", path, mode);

    struct uio uio = PROC_UIO(cur_thread->owner);

    int ret = vfs_mkdir(path, mode, &uio, NULL);
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_uname(struct utsname *name)
{
    syscall_log(LOG_DEBUG, "uname(name=%p)\n", name);

    /* FIXME: Sanity checking */

    strcpy(name->sysname,  UTSNAME_SYSNAME);
    strcpy(name->nodename, UTSNAME_NODENAME);
    strcpy(name->release,  UTSNAME_RELEASE);
    strcpy(name->version,  UTSNAME_VERSION);
    strcpy(name->machine,  UTSNAME_MACHINE);

    arch_syscall_return(cur_thread, 0);
    return;
}

static void sys_pipe(int fd[2])
{
    syscall_log(LOG_DEBUG, "pipe(fd=%p)\n", fd);
    int fd1 = proc_fd_get(cur_thread->owner);
    int fd2 = proc_fd_get(cur_thread->owner);
    pipefs_pipe(&cur_thread->owner->fds[fd1], &cur_thread->owner->fds[fd2]);
    fd[0] = fd1;
    fd[1] = fd2;
    arch_syscall_return(cur_thread, 0);
}

static void sys_fcntl(int fd, int cmd, uintptr_t arg)
{
    syscall_log(LOG_DEBUG, "fcntl(fd=%d, cmd=%d, arg=0x%x)\n",
            fd, cmd, arg);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];

    int dupfd = 0;

    switch (cmd) {
        case F_DUPFD:
            if (arg == 0)
                dupfd = proc_fd_get(cur_thread->owner);
            else
                dupfd = arg;
            cur_thread->owner->fds[dupfd] = *file;
            arch_syscall_return(cur_thread, dupfd);
            return;
        case F_GETFD:
            arch_syscall_return(cur_thread, file->flags);
            return;
        case F_SETFD:
            file->flags = (int) arg; /* XXX */
            arch_syscall_return(cur_thread, 0);
            return;
    }

    arch_syscall_return(cur_thread, -EINVAL);
}

static void sys_chdir(const char *path)
{
    syscall_log(LOG_DEBUG, "chdir(path=%s)\n", path);

    if (!path || !*path) {
        arch_syscall_return(cur_thread, -ENOENT);
        return;
    }

    int ret = 0;
    char *abs_path = NULL;

    struct vnode vnode;
    ret = vfs_lookup(path, &PROC_UIO(cur_thread->owner), &vnode, &abs_path);

    if (ret)
        goto free_resources;

    if (!S_ISDIR(vnode.mode)) {
        ret = -ENOTDIR;
        goto free_resources;
    }

    kfree(cur_thread->owner->cwd);
    cur_thread->owner->cwd = strdup(abs_path);

free_resources:
    kfree(abs_path);
    arch_syscall_return(cur_thread, ret);
}

static void sys_getcwd(char *buf, size_t size)
{
    syscall_log(LOG_DEBUG, "getcwd(buf=%p, size=%d)\n", buf, size);

    if (!size) {
        arch_syscall_return(cur_thread, -EINVAL);
        return;
    }

    size_t len = strlen(cur_thread->owner->cwd);

    if (size < len + 1) {
        arch_syscall_return(cur_thread, -ERANGE);
        return;
    }

    memcpy(buf, cur_thread->owner->cwd, len + 1);
    arch_syscall_return(cur_thread, 0);
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
    syscall_log(LOG_DEBUG,
            "thread_create(stack=%p, entry=%p, uentry=%p, arg=%p, attr=%p)\n",
            __uthread->stack, __uthread->entry, __uthread->uentry,
            __uthread->arg, __uthread->attr);

    struct thread *thread;
    thread_create(cur_thread, __uthread->stack, __uthread->entry, __uthread->uentry, __uthread->arg, __uthread->attr, &thread);
    sched_thread_ready(thread);
    arch_syscall_return(cur_thread, thread->tid);
}

static void sys_thread_exit(void *value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_exit(value_ptr=%p)\n", value_ptr);

    //cur_thread->value_ptr = value_ptr;
    struct proc *owner = cur_thread->owner;

    thread_kill(cur_thread);

    /* Wakeup owner if it is waiting for joining */
    thread_queue_wakeup(&owner->thread_join);

    arch_sleep();

    for (;;);
}

static void sys_thread_join(int tid, void **value_ptr)
{
    syscall_log(LOG_DEBUG, "thread_join(tid=%d, value_ptr=%p)\n", tid, value_ptr);

    struct proc *owner = cur_thread->owner;
    struct thread *thread = NULL;

    for (struct qnode *node = owner->threads.head; node; node = node->next) {
        struct thread *_thread = node->value;
        if (_thread->tid == tid)
            thread = _thread;
    }

    /* No such thread */
    if (!thread) {
        arch_syscall_return(cur_thread, -ECHILD);
        return;
    }

    if (thread->state == ZOMBIE) {  /* Thread is terminated */
        //*value_ptr = thread->value_ptr;
        arch_syscall_return(cur_thread, thread->tid);
        return;
    }

    while (thread->state != ZOMBIE) {
        if (thread_queue_sleep(&cur_thread->owner->thread_join)) {
            arch_syscall_return(cur_thread, -EINTR);
            return;
        }
    }

    //*value_ptr = thread->value_ptr;
    arch_syscall_return(cur_thread, thread->tid);
}

static void sys_setpgid(pid_t pid, pid_t pgid)
{
    syscall_log(LOG_DEBUG, "setpgid(pid=%d, pgid=%d)\n", pid, pgid);

    //if (pid == 0 && pgid == 0) {
        int ret = pgrp_new(cur_thread->owner, NULL);
        arch_syscall_return(cur_thread, ret);
    //} else {
    //    panic("Unsupported");
    //}
}

static void sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    syscall_log(LOG_DEBUG, "mknod(path=%s, mode=%x, dev=%x)\n", path, mode, dev);

    int ret = vfs_mknod(path, mode, dev, &PROC_UIO(cur_thread->owner), NULL);
    arch_syscall_return(cur_thread, ret);
}

static void sys_lstat(const char *path, struct stat *buf)
{
    syscall_log(LOG_DEBUG, "lstat(path=%s, buf=%p)\n", path, buf);

    struct vnode vnode;
    struct inode *inode = NULL;
    int ret = 0;
    struct uio uio = PROC_UIO(cur_thread->owner);
    uio.flags = O_NOFOLLOW;

    if ((ret = vfs_lookup(path, &uio, &vnode, NULL))) {
        arch_syscall_return(cur_thread, ret);
        return;
    }

    if ((ret = vfs_vget(&vnode, &inode))) {
        arch_syscall_return(cur_thread, ret);
        return;
    }

    ret = vfs_stat(inode, buf);
    arch_syscall_return(cur_thread, ret);
}

static void sys_auth(uint32_t uid, const char *pw)
{
    syscall_log(LOG_DEBUG, "auth(uid=%d, pw=%s)\n", uid, pw);

    cur_thread->owner->uid = uid;   /* XXX */
    arch_syscall_return(cur_thread, 0);
}

static void sys_getuid(void)
{
    syscall_log(LOG_DEBUG, "getuid()\n");
    arch_syscall_return(cur_thread, cur_thread->owner->uid);
}

static void sys_getgid(void)
{
    syscall_log(LOG_DEBUG, "getgid()\n");
    arch_syscall_return(cur_thread, cur_thread->owner->gid);
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
    syscall_log(LOG_DEBUG, "mmap(addr=%p, len=%d, prot=%x, flags=%x, fildes=%d, off=%d, ret=%p)\n",
            args->addr, args->len, args->prot, args->flags, args->fildes, args->off, ret);

    int err = 0;

    int fildes = args->fildes;
    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];

    if (!file->inode) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    /* Allocate VMR */
    struct vm_entry *vm_entry;
    vm_entry = kmalloc(sizeof(struct vm_entry), &M_VM_ENTRY, 0);

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
    vm_entry->flags |= VM_FILE;  /* TODO Support anonymous maps */
    //vm_entry->inode  = file->inode;
    vm_entry->vm_object = vm_object_inode(file->inode);
    vm_entry->off    = args->off;

    if (!(args->flags & MAP_FIXED))
        vm_entry->base = 0;  /* Allocate memory region */

    if ((err = vm_entry_insert(&cur_thread->owner->vm_space, vm_entry)))
        goto error;

    if (!(args->flags & MAP_PRIVATE) && (err = vfs_map(vm_entry)))
        goto error;

    *ret = (void *) vm_entry->base;

    arch_syscall_return(cur_thread, err);
    return;

error:
    if (vm_entry) {
        if (vm_entry->qnode)
            queue_node_remove(&cur_thread->owner->vm_space.vm_entries, vm_entry->qnode);

        kfree(vm_entry);
    }

    arch_syscall_return(cur_thread, err);
    return;
}

static void sys_munmap(void *addr, size_t len)
{
    syscall_log(LOG_DEBUG, "munmap(addr=%p, len=%d)\n", addr, len);

    struct vm_space *vm_space = &cur_thread->owner->vm_space;

    for (struct qnode *node = vm_space->vm_entries.head; node; node = node->next) {
        struct vm_entry *vm_entry = node->value;

        if (vm_entry->base == (uintptr_t) addr && vm_entry->size == len) {
            queue_node_remove(&vm_space->vm_entries, vm_entry->qnode);
            vm_unmap_full(vm_space, vm_entry);
            kfree(vm_entry);
            arch_syscall_return(cur_thread, 0);
            return;
        }
    }

    /* Not found */
    arch_syscall_return(cur_thread, -EINVAL);
    return;
}

static void sys_socket(int domain, int type, int protocol)
{
    syscall_log(LOG_DEBUG, "socket(domain=%d, type=%d, protocol=%d)\n",
            domain, type, protocol);

    int fd = proc_fd_get(cur_thread->owner);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(cur_thread, -EMFILE);
        return;
    }
    
    int err = 0;
    struct file *file = &cur_thread->owner->fds[fd];

    if ((err = socket_create(file, domain, type, protocol))) {
        proc_fd_release(cur_thread->owner, fd);
        arch_syscall_return(cur_thread, err);
        return;
    }

    arch_syscall_return(cur_thread, fd);
    return;
}

static void sys_accept(int fd, const struct sockaddr *addr, uint32_t *len)
{
    syscall_log(LOG_DEBUG, "accept(fd=%d, addr=%p, len=%p)\n",
            fd, addr, len);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    int conn_fd = proc_fd_get(cur_thread->owner);

    if (conn_fd == -1) {
        arch_syscall_return(cur_thread, -EMFILE);
        return; 
    }

    struct file *socket = &cur_thread->owner->fds[fd];
    struct file *conn   = &cur_thread->owner->fds[conn_fd];

    int err = 0;
    if ((err = socket_accept(socket, conn, addr, len))) {
        proc_fd_release(cur_thread->owner, conn_fd);
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, conn_fd);
    return;
}

static void sys_bind(int fd, const struct sockaddr *addr, uint32_t len)
{
    syscall_log(LOG_DEBUG, "bind(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];

    if (!file) {
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    int err = 0;

    if ((err = socket_bind(file, addr, len))) {
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, 0);
    return;
}

static void sys_connect(int fd, const struct sockaddr *addr, uint32_t len)
{
    syscall_log(LOG_DEBUG, "connect(fd=%d, addr=%p, len=%d)\n",
            fd, addr, len);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *socket = &cur_thread->owner->fds[fd];
    int err = 0;

    if ((err = socket_connect(socket, addr, len))) {
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, 0);
    return;
}

static void sys_listen(int fd, int backlog)
{
    syscall_log(LOG_DEBUG, "listen(fd=%d, backlog=%d)\n", fd, backlog);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];

    if (!file) {
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    int err = 0;

    if ((err = socket_listen(file, backlog))) {
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, 0);
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
    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "send(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];

    if (!file) {
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    int err = 0;

    if ((err = socket_send(file, buf, len, flags))) {
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, 0);
    return;
}

static void sys_recv(struct socket_io_syscall *s)
{
    int fd = s->fd;
    void *buf = s->buf;
    size_t len = s->len;
    int flags = s->flags;

    syscall_log(LOG_DEBUG, "recv(fd=%d, buf=%p, len=%d, flags=%x)\n",
            fd, buf, len, flags);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fd];

    if (!file) {
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    int err = 0;

    if ((err = socket_recv(file, buf, len, flags))) {
        arch_syscall_return(cur_thread, err);
        return; 
    }

    arch_syscall_return(cur_thread, 0);
    return;
}

static void sys_umask(mode_t mask)
{
    syscall_log(LOG_DEBUG, "umask(mask=%d)\n", mask);

    mode_t cur_mask = cur_thread->owner->mask;
    cur_thread->owner->mask = mask & 0777;

    arch_syscall_return(cur_thread, cur_mask);
    return;
}

static void sys_chmod(const char *path, mode_t mode)
{
    syscall_log(LOG_DEBUG, "chmod(path=%s, mode=%d)\n", path, mode);
    arch_syscall_return(cur_thread, -ENOSYS);
}

static void sys_sysconf(int name)
{
    syscall_log(LOG_DEBUG, "sysconf(name=%d)\n", name);
    arch_syscall_return(cur_thread, -ENOSYS);
}

#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4

static void sys_access(const char *path, int mode)
{
    syscall_log(LOG_DEBUG, "access(path=%s, mode=%d)\n", path, mode);

    int err = 0;

    /* Look up the file */
    struct vnode vnode;
    struct inode *inode = NULL;
    struct file  file;

    struct uio uio = PROC_UIO(cur_thread->owner);
    uio.flags = O_RDONLY; /* XXX */

    err = vfs_lookup(path, &uio, &vnode, NULL);

    if (err) goto done;

    err = vfs_vget(&vnode, &inode);

    if (err) goto done;

    file = (struct file) {
        .inode  = inode,
        .offset = 0,
        .flags  = uio.flags,
    };

    if ((err = vfs_perms_check(&file, &uio)))
        goto done;

done:
    if (inode)
        vfs_close(inode);

    arch_syscall_return(cur_thread, err);

    return;
}

static void sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    syscall_log(LOG_DEBUG, "gettimeofday(tv=%p, tz=%p)\n", tv, tz);
    arch_syscall_return(cur_thread, gettimeofday(tv, tz));
    return;
}

static void sys_sigmask(int how, void *set, void *oldset)
{
    syscall_log(LOG_DEBUG, "sigmask(how=%d, set=%p, oldset=%p)\n", how, set, oldset);
    arch_syscall_return(cur_thread, -ENOTSUP);
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

    int nfds = args->nfds;
    fd_set *readfds = args->readfds;
    fd_set *writefds = args->writefds;
    fd_set *exceptfds = args->exceptfds;
    struct timeval *timeout = args->timeout;

    int count = 0;

    for (int i = 0; i < nfds; ++i) {
        if (readfds && readfds->fds_bits[i/NFDBITS] & (1 << (i % NFDBITS))) {
            struct file *file = &cur_thread->owner->fds[i];
            if (vfs_file_can_read(file, 1) > 0) {
                readfds->fds_bits[i/NFDBITS] |= (1 << (i % NFDBITS));
                ++count;
            } else {
                readfds->fds_bits[i/NFDBITS] &= ~(1 << (i % NFDBITS));
            }
        }

        if (writefds && writefds->fds_bits[i/NFDBITS] & (1 << (i % NFDBITS))) {
            struct file *file = &cur_thread->owner->fds[i];
            if (vfs_file_can_write(file, 1) > 0) {
                writefds->fds_bits[i/NFDBITS] |= (1 << (i % NFDBITS));
                ++count;
            } else {
                writefds->fds_bits[i/NFDBITS] &= ~(1 << (i % NFDBITS));
            }
        }
    }

    arch_syscall_return(cur_thread, count);
}

static void sys_getpgrp(void)
{
    syscall_log(LOG_DEBUG, "getpgrp()\n");
    arch_syscall_return(cur_thread, cur_thread->owner->pgrp->pgid);
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
    /* 35 */    sys_auth,
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
};

const size_t syscall_cnt = sizeof(syscall_table)/sizeof(syscall_table[0]);
