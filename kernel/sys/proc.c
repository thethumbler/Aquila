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
#include <core/panic.h>

#include <core/string.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <fs/vfs.h>
#include <ds/queue.h>
#include <ds/bitmap.h>

MALLOC_DEFINE(M_PROC, "proc", "process structure");
MALLOC_DEFINE(M_SESSION, "session", "session structure");
MALLOC_DEFINE(M_PGROUP, "pgroup", "process group structure");
MALLOC_DEFINE(M_FDS, "fds", "file descriptor array"); /* FIXME */

/* all processes */
struct queue *procs = QUEUE_NEW();

/* all sessions */
struct queue *sessions = QUEUE_NEW();

/* all process groups */
struct queue *pgroups = QUEUE_NEW();

bitmap_t *pid_bitmap = BITMAP_NEW(4096);
static int ff_pid = 1;

int proc_pid_alloc()
{
    for (int i = ff_pid; (size_t) i < pid_bitmap->max_idx; ++i) {
        if (!bitmap_check(pid_bitmap, i)) {
            bitmap_set(pid_bitmap, i);
            ff_pid = i;
            return i;
        }
    }

    return -1;
}

void proc_pid_free(int pid)
{
    bitmap_clear(pid_bitmap, pid);
    if (pid < ff_pid)
        ff_pid = pid;
}

int proc_new(struct proc **ref)
{
    int err = 0;
    struct proc *proc = NULL;
    struct thread *thread = NULL;
    struct pmap *pmap = NULL;

    proc = kmalloc(sizeof(struct proc), &M_PROC, 0);

    if (!proc) {
        err = -ENOMEM;
        goto error;
    }

    memset(proc, 0, sizeof(struct proc));

    err = thread_new(proc, &thread);

    if (err != 0)
        goto error;

    pmap = arch_pmap_create();

    if (pmap == NULL) {
        err = -ENOMEM;
        goto error;
    }

    proc->vm_space.pmap = pmap;

    /* Set all signal handlers to default */
    for (int i = 0; i < SIG_MAX; ++i)
        proc->sigaction[i].sa_handler = SIG_DFL;

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

struct proc *proc_pid_find(pid_t pid)
{
    for (struct qnode *node = procs->head; node; node = node->next) {
        struct proc *proc = node->value;
        if (proc->pid == pid)
            return proc;
    }
    
    return NULL;
}

int proc_init(struct proc *proc)
{
    int err = 0;

    if (!proc)
        return -EINVAL;

    proc->pid = proc_pid_alloc();

    proc->fds = kmalloc(FDS_COUNT * sizeof(struct file), &M_FDS, 0);

    if (!proc->fds) {
        err = -ENOMEM;
        goto error;
    }

    memset(proc->fds, 0, FDS_COUNT * sizeof(struct file));

    proc->sig_queue = queue_new();  /* Initalize signals queue */

    if (!proc->sig_queue) {
        err = -ENOMEM;
        goto error;
    }

    return 0;

error:
    if (proc->fds)
        kfree(proc->fds);

    if (proc->sig_queue)
        kfree(proc->sig_queue);

    return err;
}

void proc_kill(struct proc *proc)
{
    if (proc->pid == 1) {
        if (proc->exit)
            panic("init killed");

        printk("kernel: reached target reboot\n");
        arch_reboot();

        panic("reboot not implemented\n");
    }

    proc->running = 0;

    int kill_cur_thread = 0;

    /* Kill all threads */
    while (proc->threads.count) {
        struct thread *thread = dequeue(&proc->threads);

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

    /* close all file descriptors */
    for (int i = 0; i < FDS_COUNT; ++i) {
        struct file *file = &proc->fds[i];
        if (file->inode && file->inode != (void *) -1) {
            vfs_file_close(file);
            file->inode = NULL;
        }
    }

    struct vm_space *vm_space = &proc->vm_space;
    vm_space_destroy(vm_space);
    arch_pmap_decref(vm_space->pmap);

    /* Free kernel-space resources */
    kfree(proc->fds);
    kfree(proc->cwd);

    while (proc->sig_queue->count)
        dequeue(proc->sig_queue);

    kfree(proc->sig_queue);

    /* Mark all children as orphans */
    for (struct qnode *node = procs->head; node; node = node->next) {
        struct proc *_proc = node->value;

        if (_proc->parent == proc)
            _proc->parent = NULL;
    }

    kfree(proc->name);

    /* XXX */
    queue_node_remove(proc->pgrp->procs, proc->pgrp_node);

    /* Wakeup parent if it is waiting for children */
    if (proc->parent) {
        thread_queue_wakeup(&proc->parent->wait_queue);
        signal_proc_send(proc->parent, SIGCHLD);
    } else { 
        /* Orphan zombie, just reap it */
        proc_reap(proc);
    }

    if (kill_cur_thread) {
        arch_cur_thread_kill();
        panic("How did we get here?");
    }
}

int proc_reap(struct proc *proc)
{
    proc_pid_free(proc->pid);

    queue_remove(procs, proc);
    kfree(proc);

    return 0;
}

int proc_fd_get(struct proc *proc)
{
    for (int i = 0; i < FDS_COUNT; ++i) {
        if (!proc->fds[i].inode) {
            proc->fds[i].inode = (void *) -1;    
            return i;
        }
    }

    return -1;
}

void proc_fd_release(struct proc *proc, int fd)
{
    if (fd < FDS_COUNT) {
        proc->fds[fd].inode = NULL;
    }
}

int session_new(struct proc *proc)
{
    int err = 0;
    struct session *session = NULL;
    struct pgroup  *pgrp    = NULL;

    /* allocate a new session structure */
    session = kmalloc(sizeof(struct session), &M_SESSION, 0);
    if (!session) goto e_nomem;

    memset(session, 0, sizeof(struct session));

    /* allocate a new process group structure for the session */
    pgrp = kmalloc(sizeof(struct pgroup), &M_PGROUP, 0);
    if (!pgrp) goto e_nomem;

    memset(pgrp, 0, sizeof(struct pgroup));

    session->pgps = queue_new();
    if (!session->pgps) goto e_nomem;

    pgrp->procs = queue_new();
    if (!pgrp->procs) goto e_nomem;

    pgrp->session_node = enqueue(session->pgps, pgrp);
    if (!pgrp->session_node) goto e_nomem;

    proc->pgrp_node = enqueue(pgrp->procs, proc);
    if (!proc->pgrp_node) goto e_nomem;

    session->sid = proc->pid;
    pgrp->pgid = proc->pid;

    session->leader = proc;
    pgrp->leader = proc;

    pgrp->session = session;
    proc->pgrp = pgrp;

    return 0;

e_nomem:
    err = -ENOMEM;
error:
    if (session) {
        kfree(session->pgps);
        kfree(session);
    }

    if (pgrp) {
        kfree(pgrp->procs);
        kfree(pgrp);
    }

    return err;
}

int pgrp_new(struct proc *proc, struct pgroup **ref)
{
    int err = 0;
    struct pgroup *pgrp = NULL;
    
    pgrp = kmalloc(sizeof(struct pgroup), &M_PGROUP, 0);
    if (!pgrp) goto e_nomem;

    memset(pgrp, 0, sizeof(struct pgroup));

    pgrp->pgid = proc->pid;
    pgrp->session = proc->pgrp->session;

    /* remove the process from the old process group */
    queue_node_remove(proc->pgrp->procs, proc->pgrp_node);

    pgrp->procs = queue_new();
    if (!pgrp->procs) goto e_nomem;

    proc->pgrp_node = enqueue(pgrp->procs, proc);
    if (!proc->pgrp_node) goto e_nomem;

    pgrp->session_node = enqueue(proc->pgrp->session->pgps, pgrp);
    if (!pgrp->session_node) goto e_nomem;

    if (!proc->pgrp->procs->count) {
        /* TODO */
    }

    proc->pgrp = pgrp;

    if (ref) *ref = pgrp;

    return 0;

e_nomem:
    err = -ENOMEM;

error:
    if (pgrp)
        kfree(pgrp);

    return err;
}
