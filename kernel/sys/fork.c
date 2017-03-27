/*
 *          fork() syscall handler
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <mm/mm.h>

#include <sys/proc.h>

proc_t *fork_proc(proc_t *proc)
{
    proc_t *fork = kmalloc(sizeof(proc_t));
    memcpy(fork, proc, sizeof(proc_t));

    fork->name = strdup(proc->name);
    fork->pid = get_pid();
    fork->parent = proc;
    fork->spawned = 1;
    
    fork->fds = kmalloc(FDS_COUNT * sizeof(struct file));
    memcpy(fork->fds, proc->fds, FDS_COUNT * sizeof(struct file));

    int retval = arch_sys_fork(fork);

    if (!retval) {
        arch_syscall_return(fork, 0);
        arch_syscall_return(proc, fork->pid);
    } else {
        arch_syscall_return(proc, retval);
        kfree(fork->fds);
        kfree(fork);
        return NULL;
    }

    return fork;
}
