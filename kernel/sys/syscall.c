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

    if (fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    cur_proc->fds[fildes].node = NULL;  /* FIXME */
    arch_syscall_return(cur_proc, 0);
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

}

static void sys_getpid()
{
    printk("[%d] %s: getpid()\n", cur_proc->pid, cur_proc->name);
    arch_syscall_return(cur_proc, cur_proc->pid);
}

static void sys_isatty()
{

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

static void sys_lseek()
{

}

static void sys_open(const char *path, int oflags)
{
    printk("[%d] %s: open(path=%s, oflags=0x%x)\n", cur_proc->pid, cur_proc->name, path, oflags);
    
    /* Look up the file */
    struct fs_node *node = vfs.find(vfs_root, path);

    if (!node) {    /* File not found */
        arch_syscall_return(cur_proc, -ENOENT);
        return;
    }

    int fd = get_fd(cur_proc);  /* Find a free file descriptor */

    if (fd == -1) {     /* No free file descriptor */
        /* Reached maximum number of open file descriptors */
        arch_syscall_return(cur_proc, -EMFILE);
        return;
    }

    cur_proc->fds[fd] = (struct file) {
        .node = node,
        .offset = 0,
        .flags = oflags,
    };

    int ret = node->fs->f_ops.open(&cur_proc->fds[fd]);
    
    if (ret) {  /* open returned an error code */
        arch_syscall_return(cur_proc, ret);
        release_fd(cur_proc, fd);
    } else {
        arch_syscall_return(cur_proc, fd);
    }

    return;
}

static void sys_read(int fildes, void *buf, size_t nbytes)
{
    printk("[%d] %s: read(fd=%d, buf=%p, count=%d)\n", cur_proc->pid, cur_proc->name, fildes, buf, nbytes);
    
    if (fildes < 0 || fildes >= FDS_COUNT) {  /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct fs_node *node = cur_proc->fds[fildes].node;

    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }

    ssize_t ret = node->fs->f_ops.read(&cur_proc->fds[fildes], buf, nbytes);
    arch_syscall_return(cur_proc, ret);

    return;
}

static void sys_sbrk(ptrdiff_t incr)
{
    printk("[%d] %s: sbrk(incr=%d)\n", cur_proc->pid, cur_proc->name, incr);

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

static void sys_write(int fd, void *buf, size_t count)
{
    printk("[%d] %s: write(fd=%d, buf=%p, count=%d)\n", cur_proc->pid, cur_proc->name, fd, buf, count);
    
    if (fd < 0 || fd >= FDS_COUNT) {   /* Out of bounds */
        arch_syscall_return(cur_proc, -EBADFD);
        return; 
    }

    struct fs_node *node = cur_proc->fds[fd].node;
    
    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }

    int ret = node->fs->f_ops.write(&cur_proc->fds[fd], buf, count);
    arch_syscall_return(cur_proc, ret);
    
    return;
}

static void sys_ioctl(int fd, int request, void *argp)
{
    printk("[%d] %s: ioctl(fd=%d, request=0x%x, argp=%p)\n", cur_proc->pid, cur_proc->name, fd, request, argp);

    struct fs_node *node = cur_proc->fds[fd].node;

    int ret = vfs.ioctl(node, request, argp);
    arch_syscall_return(cur_proc, ret);
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

    struct fs_node *node = cur_proc->fds[fd].node;

    if (!node) {    /* Invalid File Descriptor */
        arch_syscall_return(cur_proc, -EBADFD);
        return;
    }
    
    int ret = node->fs->f_ops.readdir(&cur_proc->fds[fd], dirent);
    arch_syscall_return(cur_proc, ret);

    return;
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
};
