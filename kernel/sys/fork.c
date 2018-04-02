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
#include <ds/queue.h>
#include <bits/errno.h>

static int copy_fds(proc_t *parent, proc_t *fork)
{
    /* Copy open files descriptors */
    fork->fds = kmalloc(FDS_COUNT * sizeof(struct file));

    if (!fork->fds)
        return -ENOMEM;

    memcpy(fork->fds, parent->fds, FDS_COUNT * sizeof(struct file));

    return 0;
}

static int copy_proc(proc_t *parent, proc_t *fork)
{
    fork->pgrp = parent->pgrp;
    fork->pgrp_node = enqueue(fork->pgrp->procs, fork);

    fork->mask = parent->mask;
    fork->uid  = parent->uid;
    fork->gid  = parent->gid;

	fork->heap_start = parent->heap_start;
	fork->heap  = parent->heap;
	fork->entry = parent->entry;

    memcpy(fork->sigaction, parent->sigaction, sizeof(parent->sigaction));

    return 0;
}

int proc_fork(thread_t *thread, proc_t **fork_ref)
{
    printk("proc_fork(fork_ref=%p)\n", fork_ref);
    int err = 0;

    /* Copy parent proc structure */
    proc_t *fork = proc_new();
    thread_t *fthread = (thread_t *) fork->threads.head->value;

    if (!fork)
        return -ENOMEM;

    proc_t *proc = thread->owner;

    //memcpy(fork, proc, sizeof(proc_t));
    copy_proc(proc, fork);

    fork->name = strdup(proc->name);

    if (!fork->name) {
        err = -ENOMEM;
        goto free_resources;
    }

    fork->pid = proc_pid_get();
    fork->parent = proc;
    fthread->spawned = 1;
    fork->cwd = strdup(proc->cwd);

    if (!fork->cwd) {
        err = -ENOMEM;
        goto free_resources;
    }
    
    /* Allocate new signals queue */
    fork->sig_queue = queue_new();

    if (!fork->sig_queue) {
        err = -ENOMEM;
        goto free_resources;
    }

    if ((err = copy_fds(proc, fork)))
        goto free_resources;

    /* Call arch specific fork handler */
    err = arch_proc_fork(thread, fork);
    //printk("fthread %p, fork->thread %p\n", fthread, fork->thread);
    //printk("fthread->arch %p, fork->thread->arch %p\n", fthread->arch, fork->thread->arch);

    if (!err) {
        arch_syscall_return(fthread, 0);
        arch_syscall_return(thread, fork->pid);
    } else {
        arch_syscall_return(thread, err);
        goto free_resources;
    }

    if (fork_ref)
        *fork_ref = fork;

    return 0;

free_resources:
    if (fork) {
        if (fork->name)
            kfree(fork->name);
        if (fork->cwd)
            kfree(fork->cwd);
        if (fork->sig_queue)
            kfree(fork->sig_queue);

        kfree(fork);
    }

    return err;
}
