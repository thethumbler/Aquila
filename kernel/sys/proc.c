/*
 *          Process managment helpers
 *
 *
 *  This file is part of AquilaOS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */


#include <core/system.h>
#include <core/string.h>
#include <core/arch.h>

#include <mm/mm.h>
#include <mm/vm.h>

#include <sys/proc.h>
#include <sys/elf.h>
#include <sys/sched.h>

#include <fs/vfs.h>

#include <ds/queue.h>
#include <ds/bitmap.h>

queue_t *procs = QUEUE_NEW(); /* All processes queue */

bitmap_t pid_bitmap = BITMAP_NEW(4096);
static int ff_pid = 1;

int proc_pid_alloc()
{
    for (int i = ff_pid; (size_t) i < pid_bitmap.max_idx; ++i) {
        if (!bitmap_check(&pid_bitmap, i)) {
            bitmap_set(&pid_bitmap, i);
            ff_pid = i;
            return i;
        }
    }

    return -1;
}

void proc_pid_free(int pid)
{
    bitmap_clear(&pid_bitmap, pid);
    if (pid < ff_pid)
        ff_pid = pid;
}

int proc_new(proc_t **ref)
{
    int err = 0;
    proc_t *proc = NULL;
    thread_t *thread = NULL;
    
    if (!(proc = kmalloc(sizeof(proc_t)))) {
        err = -ENOMEM;
        goto error;
    }

    memset(proc, 0, sizeof(proc_t));

    if ((err = thread_new(proc, &thread)))
        goto error;

    proc->running = 1;

    enqueue(procs, proc);   /* Add process to all processes queue */

    if (ref)
        *ref = proc;

    return 0;

error:
    if (proc)
        kfree(proc);

    return err;
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

    proc->pid = proc_pid_alloc();

    proc->fds  = kmalloc(FDS_COUNT * sizeof(struct file));

    if (!proc->fds) {
        err = -ENOMEM;
        goto free_resources;
    }

    memset(proc->fds, 0, FDS_COUNT * sizeof(struct file));

    proc->sig_queue = queue_new();  /* Initalize signals queue */
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
    if (proc->pid == 1)
        panic("init killed\n");

    proc->running = 0;

    /* Free resources */
    arch_proc_kill(proc);

    /* Unmap VMRs */
    struct vmr *vmr = NULL;
    while ((vmr = dequeue(&proc->vmr))) {
        vm_unmap_full(vmr);
        kfree(vmr);
    }

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

    int kill_cur_thread = 0;

    /* Kill all threads */
    while (proc->threads.count) {
        thread_t *thread = dequeue(&proc->threads);

        if (thread->sleep_node) /* Thread is sleeping on some queue */
            queue_node_remove(thread->sleep_queue, thread->sleep_node);

        if (thread->sched_node) /* Thread is in the scheduler queue */
            queue_node_remove(thread->sched_queue, thread->sched_node);

        if (thread == cur_thread) {
            kill_cur_thread = 1;
            continue;
        }

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

    if (kill_cur_thread) {
        arch_cur_thread_kill();
        /* We shouldn't get here */
    }
}

int proc_reap(proc_t *proc)
{
    proc_pid_free(proc->pid);

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

    session->pgps = queue_new();
    pgrp->procs = queue_new();

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

    pgrp->procs = queue_new();
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
