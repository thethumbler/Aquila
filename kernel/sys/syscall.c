/**********************************************************************
 *                          System Calls
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */


#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/signal.h>

#include <bits/errno.h>
#include <bits/dirent.h>
#include <bits/utsname.h>
#include <bits/fcntl.h>

#include <fs/devpts.h>
#include <fs/pipe.h>
#include <fs/stat.h>

#include <mm/vm.h>

#define DEBUG_SYSCALL
#ifndef DEBUG_SYSCALL
#define printk(...) {}
#endif

static void sys_exit(int code)
{
    printk("[%d:%d] %s: exit(code=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, code);

    proc_t *owner = cur_thread->owner;

    owner->exit = _PROC_EXIT(code, 0);  /* Child exited normally */

    proc_kill(owner);
    arch_sleep();

    /* We should never reach this anyway */
    for (;;);
}

static void sys_close(int fildes)
{
    printk("[%d:%d] %s: close(fildes=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];
    
    if (!file->node) {
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    int ret = vfs_file_close(file);
    cur_thread->owner->fds[fildes].node = NULL;
    arch_syscall_return(cur_thread, ret);
}

static void sys_execve(const char *path, char *const argp[], char *const envp[])
{
    printk("[%d:%d] %s: execve(path=%s, argp=%p, envp=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, argp, envp);

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
    printk("[%d:%d] %s: fork()\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);

    proc_t *fork = NULL;
    proc_fork(cur_thread, &fork);

    /* Returns are handled inside proc_fork */
    if (fork) {
        thread_t *thread = (thread_t *) fork->threads.head->value;
        sched_thread_ready(thread);
    }
}

static void sys_fstat(int fildes, struct stat *buf)
{
    printk("[%d:%d] %s: fstat(fildes=%d, buf=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fildes, buf);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];
    
    if (!file->node) {
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    struct inode *inode = file->node;

    int ret = vfs_stat(inode, buf);
    arch_syscall_return(cur_thread, ret);
}

static void sys_getpid()
{
    printk("[%d:%d] %s: getpid()\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
    arch_syscall_return(cur_thread, cur_thread->owner->pid);
}

static void sys_isatty(int fildes)
{
    printk("[%d:%d] %s: isatty(fildes=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct inode *node = cur_thread->owner->fds[fildes].node;

    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    //arch_syscall_return(cur_thread, node->rdev & (136 << 8));
    arch_syscall_return(cur_thread, 1);
}

static void sys_kill(pid_t pid, int sig)
{
    printk("[%d:%d] %s: kill(pid=%d, sig=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, pid, sig);
    int ret = signal_send(pid, sig);
    arch_syscall_return(cur_thread, ret);
}

static void sys_link()
{

}

static void sys_lseek(int fildes, off_t offset, int whence)
{
    printk("[%d:%d] %s: lseek(fildes=%d, offset=%d, whence=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fildes, offset, whence);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_thread, -EBADFD);
        return; 
    }

    struct file *file = &cur_thread->owner->fds[fildes];

    if (!file->node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_thread, -EBADFD);
        return;
    }

    int ret = vfs_file_lseek(file, offset, whence);
    arch_syscall_return(cur_thread, ret);
}

static void sys_open(const char *path, int oflags)
{
    printk("[%d:%d] %s: open(path=%s, oflags=0x%x)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, oflags);
    
    int fd = proc_fd_get(cur_thread->owner);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(cur_thread, -EMFILE);
        return;
    }

    /* Look up the file */
    struct vnode vnode;
    struct inode *inode = NULL;
    struct uio uio = _PROC_UIO(cur_thread->owner);
    uio.flags = oflags;

    int ret = vfs_lookup(path, &uio, &vnode, NULL);

    if (ret) {   /* Lookup failed */
        if ((ret == -ENOENT) && (oflags & O_CREAT)) {
            if ((ret = vfs_creat(path, &uio, &inode))) {
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
        .node = inode,
        .offset = 0,
        .flags = oflags,
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
    printk("[%d:%d] %s: read(fd=%d, buf=%p, count=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fildes, buf, nbytes);
    
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
    printk("[%d:%d] %s: sbrk(incr=%d=0x%x)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, incr, incr);

    uintptr_t ret = cur_thread->owner->heap;
    cur_thread->owner->heap += incr;
    cur_thread->owner->heap_vmr->size += incr;

    arch_syscall_return(cur_thread, ret);

    return;
}

static void sys_stat(const char *path, struct stat *buf)
{
    printk("[%d:%d] %s: stat(path=%s, buf=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, buf);

    struct vnode vnode;
    struct inode *inode = NULL;
    int ret = 0;
    struct uio uio = _PROC_UIO(cur_thread->owner);

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
    printk("[%d:%d] %s: unlink(path=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path);

    struct uio uio = _PROC_UIO(cur_thread->owner);
    int ret = vfs_unlink(path, &uio);
    arch_syscall_return(cur_thread, ret);
}

static void sys_waitpid(int pid, int *stat_loc, int options)
{
    printk("[%d:%d] %s: waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, pid, stat_loc, options);

    proc_t *child = proc_pid_find(pid);

    /* If pid is invalid or current process is not parent of child */
    if (!child || child->parent != cur_thread->owner) {
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

static void sys_write(int fd, void *buf, size_t nbytes)
{
    printk("[%d:%d] %s: write(fd=%d, buf=%p, nbytes=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fd, buf, nbytes);
    
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
    printk("[%d:%d] %s: ioctl(fd=%d, request=0x%x, argp=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fd, request, argp);

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
    printk("[%d:%d] %s: sigaction(sig=%d, act=%p, oact=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, sig, act, oact);

    /* TODO Boundary checking */

    if (oact)
        memcpy(oact, &cur_thread->owner->sigaction[sig], sizeof(struct sigaction));

    if (act)
        memcpy(&cur_thread->owner->sigaction[sig], act, sizeof(struct sigaction));

    arch_syscall_return(cur_thread, 0);
}

static void sys_readdir(int fd, struct dirent *dirent)
{
    printk("[%d:%d] %s: readdir(fd=%d, dirent=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fd, dirent);

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

    printk("[%d:%d] %s: mount(type=%s, dir=%s, flags=%x, data=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, type, dir, flags, data);

    int ret = vfs_mount(type, dir, flags, data, &_PROC_UIO(cur_thread->owner));
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_mkdir(const char *path, mode_t mode)
{
    printk("[%d:%d] %s: mkdir(path=%s, mode=%x)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, mode);

    struct uio uio = _PROC_UIO(cur_thread->owner);
    uio.mask = mode;

    int ret = vfs_mkdir(path, &uio, NULL);
    arch_syscall_return(cur_thread, ret);
    return;
}

static void sys_uname(struct utsname *name)
{
    printk("[%d:%d] %s: uname(name=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, name);

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
    printk("[%d:%d] %s: pipe(fd=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, fd);
    int fd1 = proc_fd_get(cur_thread->owner);
    int fd2 = proc_fd_get(cur_thread->owner);
    pipefs_pipe(&cur_thread->owner->fds[fd1], &cur_thread->owner->fds[fd2]);
    fd[0] = fd1;
    fd[1] = fd2;
    arch_syscall_return(cur_thread, 0);
}

static void sys_fcntl(int fildes, int cmd, uintptr_t arg)
{
    printk("[%d] %s: fcntl(fildes=%d, cmd=%d, arg=0x%x)\n", cur_thread->owner->pid, cur_thread->owner->name, fildes, cmd, arg);
    for (;;);
    arch_syscall_return(cur_thread, 0);
}

static void sys_chdir(const char *path)
{
    printk("[%d:%d] %s: chdir(path=%s)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path);

    if (!path || !*path) {
        arch_syscall_return(cur_thread, -ENOENT);
        return;
    }

    int ret = 0;
    char *abs_path = NULL;

    struct vnode vnode;
    ret = vfs_lookup(path, &_PROC_UIO(cur_thread->owner), &vnode, &abs_path);

    if (ret)
        goto free_resources;

    if (vnode.type != FS_DIR) {
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
    printk("[%d:%d] %s: getcwd(buf=%p, size=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, buf, size);

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
    printk("[%d:%d] %s: thread_create(stack=%p, entry=%p, uentry=%p, arg=%p, attr=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, __uthread->stack, __uthread->entry, __uthread->entry, __uthread->arg, __uthread->attr);
    thread_t *thread;
    thread_create(cur_thread, __uthread->stack, __uthread->entry, __uthread->uentry, __uthread->arg, __uthread->attr, &thread);
    sched_thread_ready(thread);
    arch_syscall_return(cur_thread, thread->tid);
}

static void sys_thread_exit(void *value_ptr)
{
    printk("[%d:%d] %s: thread_exit(value_ptr=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, value_ptr);

    //cur_thread->value_ptr = value_ptr;
    proc_t *owner = cur_thread->owner;

    thread_kill(cur_thread);

    /* Wakeup owner if it is waiting for joining */
    thread_queue_wakeup(&owner->thread_join);

    arch_sleep();

    for (;;);
}

static void sys_thread_join(int tid, void **value_ptr)
{
    printk("[%d:%d] %s: thread_join(tid=%d, value_ptr=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, tid, value_ptr);

    proc_t *owner = cur_thread->owner;
    thread_t *thread = NULL;

    forlinked (node, owner->threads.head, node->next) {
        thread_t *_thread = node->value;
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
    printk("[%d:%d] %s: setpgid(pid=%d, pgid=%d)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, pid, pgid);

    if (pid == 0 && pgid == 0) {
        int ret = pgrp_new(cur_thread->owner, NULL);
        arch_syscall_return(cur_thread, ret);
    } else {
        panic("Unsupported");
    }
}

static void sys_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    printk("[%d:%d] %s: mknod(path=%s, mode=%x, dev=%x)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, mode, dev);

    itype_t type = 0;
    switch (mode & S_IFMT) {
        case S_IFCHR: type = FS_CHRDEV; break;
        case S_IFBLK: type = FS_BLKDEV; break;
    }

    int ret = vfs_mknod(path, type, dev, &_PROC_UIO(cur_thread->owner), NULL);
    arch_syscall_return(cur_thread, ret);
}

static void sys_lstat(const char *path, struct stat *buf)
{
    printk("[%d:%d] %s: lstat(path=%s, buf=%p)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, path, buf);

    struct vnode vnode;
    struct inode *inode = NULL;
    int ret = 0;
    struct uio uio = _PROC_UIO(cur_thread->owner);
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
    printk("[%d:%d] %s: auth(uid=%d, pw=%s)\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name, uid, pw);

    cur_thread->owner->uid = uid;   /* XXX */
    arch_syscall_return(cur_thread, 0);
}

static void sys_getuid(void)
{
    printk("[%d:%d] %s: getuid()\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
    arch_syscall_return(cur_thread, cur_thread->owner->uid);
}

static void sys_getgid(void)
{
    printk("[%d:%d] %s: getgid()\n", cur_thread->owner->pid, cur_thread->tid, cur_thread->owner->name);
    arch_syscall_return(cur_thread, cur_thread->owner->gid);
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
};

size_t syscall_cnt = sizeof(syscall_table)/sizeof(syscall_table[0]);
