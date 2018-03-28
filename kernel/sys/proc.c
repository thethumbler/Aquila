/*
 *          Process managment helpers
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
#include <sys/elf.h>
#include <sys/sched.h>

#include <fs/vfs.h>

#include <ds/queue.h>

queue_t *procs = NEW_QUEUE; /* All processes queue */

int proc_pid_get()
{
    /* TODO: Use bitmap */
    static int pid = 0;
    return ++pid;
}

proc_t *proc_new(void)
{
    proc_t *proc = kmalloc(sizeof(proc_t));
    memset(proc, 0, sizeof(proc_t));

    thread_t *thread;
    thread_new(proc, &thread);
    proc->running = 1;

    enqueue(procs, proc);   /* Add process to all processes queue */
    return proc;
}

proc_t *proc_pid_find(pid_t pid)
{
    forlinked (node, procs->head, node->next) {
        proc_t *proc = node->value;
        if (proc->pid == pid)
            return proc;
    }
    
    return NULL;
}

int proc_init(proc_t *proc)
{
    int err = 0;

    if (!proc)
        return -EINVAL;

    proc->pid = proc_pid_get();

    proc->fds  = kmalloc(FDS_COUNT * sizeof(struct file));

    if (!proc->fds) {
        err = -ENOMEM;
        goto free_resources;
    }

    memset(proc->fds, 0, FDS_COUNT * sizeof(struct file));

    proc->sig_queue = new_queue();  /* Initalize signals queue */
    if (!proc->sig_queue) {
        err = -ENOMEM;
        goto free_resources;
    }

    return 0;

free_resources:
    if (proc->fds)
        kfree(proc->fds);
    if (proc->sig_queue)
        kfree(proc->sig_queue);

    return err;
}

void proc_kill(proc_t *proc)
{
    proc->running = 0;

    /* Free resources */
    arch_proc_kill(proc);

    /* Unmap memory */
    pmman.unmap_full((uintptr_t) NULL, (uintptr_t) proc->heap);
    pmman.unmap_full(USER_STACK_BASE, USER_STACK_SIZE);

    /* Free kernel-space resources */
    kfree(proc->fds);
    kfree(proc->cwd);

    while (proc->sig_queue->count)
        dequeue(proc->sig_queue);

    kfree(proc->sig_queue);

    /* Mark all children as orphans */
    forlinked (node, procs->head, node->next) {
        proc_t *_proc = node->value;

        if (_proc->parent == proc)
            _proc->parent = NULL;
    }

    kfree(proc->name);

    /* Kill all threads */
    while (proc->threads.count) {
        thread_t *thread = dequeue(&proc->threads);

        if (thread->sleep_node) /* Thread is sleeping on some queue */
            queue_node_remove(thread->sleep_queue, thread->sleep_node);

        if (thread->sched_node) /* Thread is in the scheduler queue */
            queue_node_remove(thread->sched_queue, thread->sched_node);

        thread_kill(thread);
        kfree(thread);
    }

    /* XXX */
    queue_node_remove(proc->pgrp->procs, proc->pgrp_node);

    /* Wakeup parent if it is waiting for children */
    if (proc->parent) {
        thread_queue_wakeup(&proc->parent->wait_queue);
        //signal_proc_send(proc->parent, SIGCHLD);
    } else { 
        /* Orphan zombie, just reap it */
        proc_reap(proc);
    }
}

int proc_reap(proc_t *proc)
{
    queue_remove(procs, proc);
    kfree(proc);

    return 0;
}

int proc_fd_get(proc_t *proc)
{
    for (int i = 0; i < FDS_COUNT; ++i) {
        if (!proc->fds[i].node) {
            proc->fds[i].node = (void *) -1;    
            return i;
        }
    }

    return -1;
}

void proc_fd_release(proc_t *proc, int fd)
{
    if (fd < FDS_COUNT) {
        proc->fds[fd].node = NULL;
    }
}

int proc_ptr_validate(proc_t *proc, void *ptr)
{
    uintptr_t uptr = (uintptr_t) ptr;
    if (!(uptr >= proc->entry && uptr <= proc->heap))
        return 0;
    return 1;
}

int session_new(proc_t *proc)
{
    session_t *session = kmalloc(sizeof(session_t));
    pgroup_t  *pgrp = kmalloc(sizeof(pgroup_t));

    session->pgps = new_queue();
    pgrp->procs = new_queue();

    pgrp->session_node = enqueue(session->pgps, pgrp);
    proc->pgrp_node = enqueue(pgrp->procs, proc);

    session->sid = proc->pid;
    pgrp->pgid = proc->pid;

    session->leader = proc;
    pgrp->leader = proc;

    pgrp->session = session;
    proc->pgrp = pgrp;

    return 0;
}

int pgrp_new(proc_t *proc, pgroup_t **ref)
{
    pgroup_t *pgrp = kmalloc(sizeof(pgroup_t));
    memset(pgrp, 0, sizeof(pgroup_t));
    pgrp->pgid = proc->pid;

    queue_node_remove(proc->pgrp->procs, proc->pgrp_node);

    pgrp->procs = new_queue();
    proc->pgrp_node = enqueue(pgrp->procs, proc);
    pgrp->session_node = enqueue(proc->pgrp->session->pgps, pgrp);

    if (!proc->pgrp->procs->count) {
        /* TODO */
    }

    proc->pgrp = pgrp;

    if (ref)
        *ref = pgrp;

    return 0;
}
