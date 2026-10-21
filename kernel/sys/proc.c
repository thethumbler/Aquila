/*
 *          Process managment helpers
 */


/** 
 * \defgroup sys kernel/sys
 * \brief system managment
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/arch.h>

#include <mm/pmap.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <fs/vfs.h>
#include <dev/tty.h>

#include <ds/queue.h>
#include <ds/bitmap.h>

MALLOC_DEFINE(M_PROC, "proc", "process structure");
MALLOC_DEFINE(M_SESSION, "session", "session structure");
MALLOC_DEFINE(M_PGROUP, "pgroup", "process group structure");
MALLOC_DEFINE(M_FDS, "fds", "file descriptor array"); /* FIXME */

/* all processes */
struct queue *procs = QUEUE_NEW();

struct proc *initproc;

/* all sessions */
struct queue *sessions = QUEUE_NEW();

/* all process groups */

struct bitmap *pid_bitmap = BITMAP_NEW(4096);
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

    proc = kmalloc(sizeof(struct proc), &M_PROC, M_ZERO);

    if (!proc) {
        err = -ENOMEM;
        goto error;
    }

    err = thread_new(proc, &thread);

    if (err != 0)
        goto error;

    pmap = pmap_create();

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
    if (thread) {
        queue_remove(&proc->threads, thread);
        kfree(thread);
    }

    if (proc)
        kfree(proc);

    return err;
}

struct proc *proc_pid_find(pid_t pid)
{
    queue_for (node, procs) {
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

    proc->fds = kmalloc(FDS_COUNT * sizeof(struct file), &M_FDS, M_ZERO);

    if (!proc->fds) {
        err = -ENOMEM;
        goto error;
    }

    return 0;

error:
    if (proc->fds)
        kfree(proc->fds);

    return err;
}

/*
 * A process group is orphaned (POSIX) when none of its members has a
 * parent in another group of its session: no one there would continue it
 * once it stops. `gone', on its way out, doesn't count, as a member or a
 * parent; nor does init, as a parent (it isn't a shell).
 */
int pgrp_orphaned(struct pgroup *pgrp, struct proc *gone)
{
    queue_for (node, pgrp->procs) {
        struct proc *p = node->value, *parent = p->parent;

        if (p == gone || p->zombie || !parent || parent == gone || parent == initproc)
            continue;

        if (parent->pgrp != pgrp && parent->pgrp->session == pgrp->session)
            return 0;
    }

    return 1;
}

/* `gone' leaving makes `pgrp' orphaned: if one of its members is stopped,
 * SIGHUP and SIGCONT to all of them, nothing else would continue them */
static void pgrp_orphaned_hangup(struct pgroup *pgrp, struct proc *gone)
{
    int stopped = 0;

    queue_for (node, pgrp->procs) {
        struct proc *p = node->value;

        if (p != gone && p->running && p->stopped)
            stopped = 1;
    }

    if (stopped && pgrp_orphaned(pgrp, gone)) {
        signal_pgrp_send(pgrp, SIGHUP);
        signal_pgrp_send(pgrp, SIGCONT);
    }
}

/* the group of `p' is in the session of the group of `q', and another */
static int pgrp_other_same_session(struct proc *p, struct proc *q)
{
    return p->pgrp != q->pgrp && p->pgrp->session == q->pgrp->session;
}

/* `proc' is a zombie: its parent is told, and may wait for it; a parent
 * that doesn't wait for its children (SIGCHLD ignored, SA_NOCLDWAIT)
 * leaves no zombie */
static void proc_zombie_notify(struct proc *proc)
{
    struct proc *parent = proc->parent;

    if (!parent) {
        proc_reap(proc);
        return;
    }

    int nowait = parent->sigaction[SIGCHLD].sa_handler == SIG_IGN ||
                 (parent->sigaction[SIGCHLD].sa_flags & SA_NOCLDWAIT);

    thread_queue_wakeup(&parent->wait_queue);
    signal_proc_send(parent, SIGCHLD);

    if (nowait)
        proc_reap(proc);
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

    /* no more SIGALRM */
    sched_alarm(proc, 0, 0, NULL, NULL);

    int kill_curthread = 0;

    /* Kill all threads */
    while (proc->threads.count) {
        struct thread *thread = dequeue(&proc->threads);

        if (thread->sleep_node) /* Thread is sleeping on some queue */
            queue_node_remove(thread->sleep_queue, thread->sleep_node);

        sched_timer_cancel(thread);

        if (thread->sched_node) /* Thread is in the scheduler queue */
            queue_node_remove(thread->sched_queue, thread->sched_node);

        if (thread == curthread) {
            kill_curthread = 1;
            continue;
        }

        /* zombie threads (thread_exit) already released their resources */
        if (thread->state != ZOMBIE)
            thread_kill(thread);

        kfree(thread);
    }

    /* close all file descriptors */
    for (int i = 0; i < FDS_COUNT; ++i) {
        struct file *file = &proc->fds[i];
        if (file->vnode && file->vnode != (void *) -1) {
            vfs_file_close(file);
            file->vnode = NULL;
        }
    }

    /* what is left frees what the thread killing itself runs on (its map,
     * its process once reaped), it must not be switched out until it is
     * gone */
    if (kill_curthread)
        arch_interrupts_disable();

    struct vm_space *vm_space = &proc->vm_space;
    vm_space_destroy(vm_space);
    pmap_decref(vm_space->pmap);

    /* nothing may switch to the freed map */
    vm_space->pmap = kvm_space.pmap;

    /* Free kernel-space resources */
    kfree(proc->fds);
    kfree(proc->cwd);

    proc->sig_pending = 0;

    /* its own group: its parent, in another group of the session, may
     * have been what kept it from being orphaned */
    if (proc->parent && pgrp_other_same_session(proc->parent, proc))
        pgrp_orphaned_hangup(proc->pgrp, proc);

    /* the children are init's: it waits for them, the zombies among them
     * already (live ones when they exit); a child's group in another
     * group of the session may be orphaned now */
    struct qnode *node = procs->head;
    while (node) {
        struct proc *_proc = node->value;
        node = node->next;  /* proc_reap removes the current node */

        if (_proc->parent == proc) {
            _proc->parent = initproc;

            if (pgrp_other_same_session(_proc, proc))
                pgrp_orphaned_hangup(_proc->pgrp, proc);

            if (_proc->zombie)
                proc_zombie_notify(_proc);
        }
    }

    kfree(proc->name);

    /* the end of the session: the controlling terminal hangs up (a zombie
     * stays in its process group until it is reaped, waitpid matches it
     * by group) */
    struct session *session = proc->pgrp->session;

    if (session->leader == proc) {
        if (session->ctty)
            tty_hangup(session->ctty);

        session->leader = NULL;
    }

    proc->zombie = 1;

    /* its parent waits for it */
    proc_zombie_notify(proc);

    if (kill_curthread) {
        arch_cur_thread_kill();
        panic("How did we get here?");
    }
}

int proc_reap(struct proc *proc)
{
    /* the parent waited for it: its times count as the parent's children's */
    if (proc->parent) {
        proc->parent->cutime += proc->utime + proc->cutime;
        proc->parent->cstime += proc->stime + proc->cstime;
    }

    proc_pid_free(proc->pid);

    queue_node_remove(proc->pgrp->procs, proc->pgrp_node);
    pgrp_release(proc->pgrp);

    queue_remove(procs, proc);
    kfree(proc);

    return 0;
}

int proc_fd_get(struct proc *proc)
{
    for (int i = 0; i < FDS_COUNT; ++i) {
        if (!proc->fds[i].vnode) {
            /* start from a clean slot, no stale offset/flags */
            proc->fds[i] = (struct file) {0};
            proc->fds[i].vnode = (void *) -1;    
            return i;
        }
    }

    return -1;
}

void proc_fd_release(struct proc *proc, int fd)
{
    if (fd >= 0 && fd < FDS_COUNT) {
        proc->fds[fd].vnode = NULL;
    }
}

int session_new(struct proc *proc)
{
    int err = 0;
    struct session *session = NULL;
    struct pgroup  *pgrp    = NULL;

    /* allocate a new session structure */
    session = kmalloc(sizeof(struct session), &M_SESSION, M_ZERO);
    if (!session) goto e_nomem;

    /* allocate a new process group structure for the session */
    pgrp = kmalloc(sizeof(struct pgroup), &M_PGROUP, M_ZERO);
    if (!pgrp) goto e_nomem;

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

/**
 * \ingroup sys
 * \brief move `proc' into the process group `pgid' of its session
 *
 * Joining a group requires it to exist (and have members), only a process
 * can start a group with its own pid as the id.
 */
int proc_setpgid(struct proc *proc, pid_t pgid)
{
    struct session *session = proc->pgrp->session;
    struct pgroup *pgrp = NULL;

    if (proc->pgrp->pgid == pgid)
        return 0;

    queue_for (node, session->pgps) {
        struct pgroup *pg = node->value;

        if (pg->pgid == pgid) {
            pgrp = pg;
            break;
        }
    }

    if ((!pgrp || !pgrp->procs->count) && pgid != proc->pid)
        return -EPERM;

    if (!pgrp) {
        if (!(pgrp = kmalloc(sizeof(struct pgroup), &M_PGROUP, M_ZERO)))
            return -ENOMEM;

        pgrp->pgid    = pgid;
        pgrp->session = session;

        if (!(pgrp->procs = queue_new())) {
            kfree(pgrp);
            return -ENOMEM;
        }

        if (!(pgrp->session_node = enqueue(session->pgps, pgrp))) {
            kfree(pgrp->procs);
            kfree(pgrp);
            return -ENOMEM;
        }
    }

    struct qnode *node = enqueue(pgrp->procs, proc);
    if (!node)
        return -ENOMEM;

    if (pgid == proc->pid)
        pgrp->leader = proc;

    struct pgroup *old = proc->pgrp;

    queue_node_remove(old->procs, proc->pgrp_node);
    proc->pgrp_node = node;
    proc->pgrp = pgrp;

    pgrp_release(old);
    return 0;
}

/**
 * \ingroup sys
 * \brief free `pgrp' if it has no processes left, and its session if that
 * was its last group (a terminal loses it as its foreground group)
 */
void pgrp_release(struct pgroup *pgrp)
{
    if (pgrp->procs->count)
        return;

    struct session *session = pgrp->session;

    if (session->ctty && session->ctty->fg == pgrp)
        session->ctty->fg = NULL;

    queue_node_remove(session->pgps, pgrp->session_node);
    kfree(pgrp->procs);
    kfree(pgrp);

    if (session->pgps->count)
        return;

    if (session->ctty) {
        session->ctty->session = NULL;
        session->ctty->fg = NULL;
    }

    kfree(session->pgps);
    kfree(session);
}
