/**********************************************************************
 *                          System Calls
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016-2017 Mohamed Anwar <mohamed_anwar@opmbx.org>
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

#include <fs/devpts.h>
#include <fs/pipe.h>

static void sys_exit(int status)
{
    printk("[%d] %s: exit(status=%d)\n", cur_proc->pid, cur_proc->name, status);

    if (cur_proc->pid == 1)
        panic("init killed\n");

    cur_proc->exit_status = status;
    kill_proc(cur_proc);

    /* Wakeup parent if it is waiting for children */
    wakeup_queue(&cur_proc->parent->wait_queue);

    /* XXX: We should signal the parent here? */

    arch_sleep();

    /* We should never reach this anyway */
    for (;;);
}

static void sys_close(int fildes)
{
    printk("[%d] %s: close(fildes=%d)\n", cur_proc->pid, cur_proc->name, fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fildes];
    
    if (!file->node) {
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }

    int ret = 0;
    //int ret = vfs.fops.close(file); /* XXX */
    cur_proc->fds[fildes].node = NULL;

    arch_syscall_return(cur_proc, ret);
}

static void sys_execve(const char *path, char *const argp[], char *const envp[])
{
    printk("[%d] %s: execve(path=%s, argp=%p, envp=%p)\n", cur_proc->pid, cur_proc->name, path, argp, envp);

    //if (!name || !strlen(name))
    //    return -ENOENT;

    char *fn = strdup(path);
    proc_t *p = execve_proc(cur_proc, fn, argp, envp);
    kfree(fn);

    if (!p)
        arch_syscall_return(cur_proc, -1);
    else
        spawn_proc(p);
}

static void sys_fork(void)
{
    printk("[%d] %s: fork()\n", cur_proc->pid, cur_proc->name);

    proc_t *fork = fork_proc(cur_proc);

    /* Returns are handled inside fork_proc */

    if (fork)
        make_ready(fork);
}

static void sys_fstat()
{
    //printk("[%d] %s: fstat(fildes=%d, buf=%p)\n", cur_proc->pid, cur_proc->name, fildes, buf);
}

static void sys_getpid()
{
    printk("[%d] %s: getpid()\n", cur_proc->pid, cur_proc->name);
    arch_syscall_return(cur_proc, cur_proc->pid);
}

static void sys_isatty(int fildes)
{
    printk("[%d] %s: isatty(fildes=%d)\n", cur_proc->pid, cur_proc->name, fildes);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct inode *node = cur_proc->fds[fildes].node;

    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }

    // XXX
    arch_syscall_return(cur_proc, !strcmp(node->dev->name, "pts"));
}

static void sys_kill(pid_t pid, int sig)
{
    printk("[%d] %s: kill(pid=%d, sig=%d)\n", cur_proc->pid, cur_proc->name, pid, sig);
    int ret = send_signal(pid, sig);
    arch_syscall_return(cur_proc, ret);
}

static void sys_link()
{

}

static void sys_lseek(int fildes, off_t offset, int whence)
{
    printk("[%d] %s: lseek(fildes=%d, offset=%d, whence=%d)\n", cur_proc->pid, cur_proc->name, fildes, offset, whence);

    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fildes];

    if (!file->node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }

    int ret = vfs.fops.lseek(file, offset, whence);
    arch_syscall_return(cur_proc, ret);
}

static void sys_open(const char *path, int oflags)
{
    printk("[%d] %s: open(path=%s, oflags=0x%x)\n", cur_proc->pid, cur_proc->name, path, oflags);
    
    int fd = get_fd(cur_proc);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(cur_proc, -EMFILE);
        return;
    }

    /* Look up the file */
    struct vnode vnode;
    int ret = vfs.lookup(path, cur_proc->uid, cur_proc->gid, &vnode);

    if (ret)    /* Lookup failed */
        goto done;

    struct inode *inode = NULL;
    ret = vfs.vget(&vnode, &inode);

    if (ret)
        goto done;

    cur_proc->fds[fd] = (struct file) {
        .node = inode,
        .offset = 0,
        .flags = oflags,
    };

    ret = vfs.fops.open(&cur_proc->fds[fd]);

done:
    if (ret < 0)  /* open returned an error code */
        release_fd(cur_proc, fd);
    else
        ret = fd;

    arch_syscall_return(cur_proc, ret);
    return;
}

static void sys_read(int fildes, void *buf, size_t nbytes)
{
    printk("[%d] %s: read(fd=%d, buf=%p, count=%d)\n", cur_proc->pid, cur_proc->name, fildes, buf, nbytes);
    
    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fildes];
    int ret = vfs.fops.read(file, buf, nbytes);
    arch_syscall_return(cur_proc, ret);
    return;
}

static void sys_sbrk(ptrdiff_t incr)
{
    printk("[%d] %s: sbrk(incr=%d, 0x%x)\n", cur_proc->pid, cur_proc->name, incr, incr);

    uintptr_t ret = cur_proc->heap;
    cur_proc->heap += incr;

    arch_syscall_return(cur_proc, ret);

    return;
}

static void sys_stat()
{

}

static void sys_times()
{

}

static void sys_unlink()
{

}

static void sys_waitpid(int pid, int *stat_loc, int options)
{
    printk("[%d] %s: waitpid(pid=%d, stat_loc=%p, options=0x%x)\n", cur_proc->pid, cur_proc->name, pid, stat_loc, options);

    proc_t *child = get_proc_by_pid(pid);

    /* If pid is invalid or current process is not parent of child */
    if (!child || child->parent != cur_proc) {
        arch_syscall_return(cur_proc, -ECHILD);
        return;
    }

    if (child->state == ZOMBIE) {  /* Child is killed */
        *stat_loc = child->exit_status;
        arch_syscall_return(cur_proc, child->pid);
        reap_proc(child);
        return;
    }

    //if (options | WNOHANG) {
    //    arch_syscall_return(0);
    //    return;
    //}

    while (child->state != ZOMBIE) {
        if (sleep_on(&cur_proc->wait_queue)) {
            arch_syscall_return(cur_proc, -EINTR);
            return;
        }
    }

    *stat_loc = child->exit_status;
    arch_syscall_return(cur_proc, child->pid);
    reap_proc(child);
}

static void sys_write(int fd, void *buf, size_t nbytes)
{
    printk("[%d] %s: write(fd=%d, buf=%p, nbytes=%d)\n", cur_proc->pid, cur_proc->name, fd, buf, nbytes);
    
    if (fd < 0 || fd >= FDS_COUNT) {   /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fd];
    int ret = vfs.fops.write(file, buf, nbytes);
    arch_syscall_return(cur_proc, ret);
    return;
}

static void sys_ioctl(int fd, int request, void *argp)
{
    printk("[%d] %s: ioctl(fd=%d, request=0x%x, argp=%p)\n", cur_proc->pid, cur_proc->name, fd, request, argp);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fd];
    int ret = vfs.fops.ioctl(file, request, argp);
    arch_syscall_return(cur_proc, ret);
    return;
}

static void sys_signal(int sig, void (*func)(int))
{
    printk("[%d] %s: signal(sig=%d, func=%p)\n", cur_proc->pid, cur_proc->name, sig, func);
    uintptr_t ret = cur_proc->signal_handler[sig];
    cur_proc->signal_handler[sig] = (uintptr_t) func;
    arch_syscall_return(cur_proc, ret);
}

static void sys_readdir(int fd, struct dirent *dirent)
{
    printk("[%d] %s: readdir(fd=%d, dirent=%p)\n", cur_proc->pid, cur_proc->name, fd, dirent);

    if (fd < 0 || fd >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct file *file = &cur_proc->fds[fd];
    int ret = vfs.fops.readdir(file, dirent);
    arch_syscall_return(cur_proc, ret);
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
    const char *type = args->type;
    const char *dir  = args->dir;
    int flags = args->flags;
    void *data = args->data;

    printk("[%d] %s: mount(type=%s, dir=%s, flags=%x, data=%p)\n", cur_proc->pid, cur_proc->name, type, dir, flags, data);

    int ret = vfs.mount(type, dir, flags, data);

    arch_syscall_return(cur_proc, ret);

    return;
}

static void sys_mkdir(const char *path, int mode)
{
    printk("[%d] %s: mkdir(path=%s, mode=%x)\n", cur_proc->pid, cur_proc->name, path, mode);

    for (;;);
    /*
    int ret = vfs.iops.mkdir(file, path, cur_proc->uid, cur_proc->gid, mode);
    arch_syscall_return(cur_proc, ret);
    return;
    */
}

static void sys_uname(struct utsname *name)
{
    printk("[%d] %s: uname(name=%p)\n", cur_proc->pid, cur_proc->name, name);

    /* FIXME: Sanity checking */

    strcpy(name->sysname,  UTSNAME_SYSNAME);
    strcpy(name->nodename, UTSNAME_NODENAME);
    strcpy(name->release,  UTSNAME_RELEASE);
    strcpy(name->version,  UTSNAME_VERSION);
    strcpy(name->machine,  UTSNAME_MACHINE);

    arch_syscall_return(cur_proc, 0);
    return;
}

static void sys_pipe(int fd[2])
{
    printk("[%d] %s: pipe(fd=%p)\n", cur_proc->pid, cur_proc->name, fd);
    int fd1 = get_fd(cur_proc);
    int fd2 = get_fd(cur_proc);
    pipefs_pipe(&cur_proc->fds[fd1], &cur_proc->fds[fd2]);
    fd[0] = fd1;
    fd[1] = fd2;
    arch_syscall_return(cur_proc, 0);
}

static void sys_fcntl(int fildes, int cmd, uintptr_t arg)
{
    printk("[%d] %s: fcntl(fildes=%d, cmd=%d, arg=0x%x)\n", cur_proc->pid, cur_proc->name, fildes, cmd, arg);
    for (;;);
    arch_syscall_return(cur_proc, 0);
}

static void sys_chdir(const char *path)
{
    printk("[%d] %s: chdir(path=%s)\n", cur_proc->pid, cur_proc->name, path);

    if (!path || !strlen(path) || path[0] == '\0') {
        arch_syscall_return(cur_proc, -ENOENT);
        return;
    }

    int rel = 0, ret = 0;
    char *p = NULL;

    if (path[0] == '/') { /* Absolute Path */
        p = (char *) path;
    } else {
        vfs.relative(cur_proc->cwd, path, &p);
        rel = 1;
    }

    struct vnode vnode;
    ret = vfs.lookup(p, cur_proc->uid, cur_proc->gid, &vnode);

    if (ret)
        goto free_resources;

    if (vnode.type != FS_DIR) {
        ret = -ENOTDIR;
        goto free_resources;
    }

    kfree(cur_proc->cwd);
    cur_proc->cwd = strdup(p);
    printk("cur_proc->cwd %s\n", cur_proc->cwd);

free_resources:
    if (rel) kfree(p);
    arch_syscall_return(cur_proc, ret);
}

static void sys_getcwd(char *buf, size_t size)
{
    printk("[%d] %s: getcwd(buf=%p, size=%d)\n", cur_proc->pid, cur_proc->name, buf, size);

    if (!size) {
        arch_syscall_return(cur_proc, -EINVAL);
        return;
    }

    size_t len = strlen(cur_proc->cwd);

    if (size < len + 1) {
        arch_syscall_return(cur_proc, -ERANGE);
        return;
    }

    memcpy(buf, cur_proc->cwd, len + 1);
    arch_syscall_return(cur_proc, 0);
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
    /* 20 */    sys_signal,
    /* 21 */    sys_readdir,
    /* 22 */    sys_mount,
    /* 23 */    sys_mkdir,
    /* 24 */    sys_uname,
    /* 25 */    sys_pipe,
    /* 26 */    sys_fcntl,
    /* 27 */    sys_chdir,
    /* 28 */    sys_getcwd,
};
