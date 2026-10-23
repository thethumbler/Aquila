/** \file
 *          Process managment helpers
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

/* the last pid given out: the next is the first free one after it,
 * wrapping around, as UNIX does (the pid of a process just reaped isn't
 * the next one: a kill() meant for it doesn't find a new process) */
static int last_pid;

/** a process group or a session has `pid' as its id: not a new pid, as
 * POSIX says (they outlive their leader) */
static int pid_is_group(int pid)
{
    queue_for (snode, sessions) {
        struct session *session = snode->value;

        if (session->sid == pid)
            return 1;

        queue_for (gnode, session->pgps) {
            struct pgroup *pgrp = gnode->value;

            if (pgrp->pgid == pid)
                return 1;
        }
    }

    return 0;
}

/** a new pid, from 1 (init's) on; -1 if none is left */
int proc_pid_alloc()
{
    size_t range = pid_bitmap->max_idx - 1;     /* 1 to max_idx - 1 */

    for (size_t n = 0; n < range; ++n) {
        int pid = 1 + (last_pid + n) % range;

        if (!bitmap_check(pid_bitmap, pid) && !pid_is_group(pid)) {
            bitmap_set(pid_bitmap, pid);
            last_pid = pid;
            return pid;
        }
    }

    return -1;
}

void proc_pid_free(int pid)
{
    bitmap_clear(pid_bitmap, pid);
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
    if (!proc)
        return -EINVAL;

    /* all taken: fork() says try again */
    if ((proc->pid = proc_pid_alloc()) < 0)
        return -EAGAIN;

    proc->fds = kmalloc(FDS_COUNT * sizeof(struct fd), &M_FDS, M_ZERO);

    if (!proc->fds) {
        proc_pid_free(proc->pid);
        return -ENOMEM;
    }

    return 0;
}

/**
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

/** `gone' leaving makes `pgrp' orphaned: if one of its members is stopped,
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

/** the group of `p' is in the session of the group of `q', and another */
static int pgrp_other_same_session(struct proc *p, struct proc *q)
{
    return p->pgrp != q->pgrp && p->pgrp->session == q->pgrp->session;
}

/** `proc' is a zombie: its parent is told, and may wait for it; a parent
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

/**
 * The threads of `proc' but `keep' end: freed, or, running on another CPU
 * (in user mode: this one has the kernel lock), killed there, freeing
 * themselves when they enter the kernel, which they do now. `exiting':
 * the process goes, they're detached from it; else they leave its list
 * when they go and wake proc->thread_join. How many run elsewhere.
 */
int proc_threads_end(struct proc *proc, struct thread *keep, int exiting)
{
    int elsewhere = 0;

    for (struct qnode *node = proc->threads.head, *next; node; node = next) {
        struct thread *thread = node->value;
        next = node->next;

        if (thread == keep)
            continue;

        if (sched_thread_cpu(thread)) {
            thread->killed = 1;
            sched_thread_kick(thread);
            elsewhere++;

            if (exiting) {
                queue_node_remove(&proc->threads, node);
                thread->owner = NULL;
            }

            continue;
        }

        queue_node_remove(&proc->threads, node);

        if (thread->sleep_node) /* Thread is sleeping on some queue */
            queue_node_remove(thread->sleep_queue, thread->sleep_node);

        sched_timer_cancel(thread);

        if (thread->sched_node) /* Thread is in the scheduler queue */
            queue_node_remove(thread->sched_queue, thread->sched_node);

        /* a system call it won't return from: the files it held */
        thread_files_release(thread);

        /* zombie threads (thread_exit) already released their resources */
        if (thread->state != ZOMBIE)
            thread_kill(thread);

        kfree(thread);
    }

    return elsewhere;
}

/**
 * exec: the threads of the process but the running one end (they run in
 * the image that is replaced, POSIX), those on other CPUs waited for
 */
void proc_threads_exec(struct proc *proc)
{
    if (!curthread || curthread->owner != proc)
        return;

    while (proc_threads_end(proc, curthread, 0))
        thread_queue_sleep_unint(&proc->thread_join);
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

    /* its threads end, the current one last (it runs this) */
    struct thread *self = curthread && curthread->owner == proc ? curthread : NULL;
    int kill_curthread = self != NULL;

    proc_threads_end(proc, self, 1);

    if (self) {
        queue_remove(&proc->threads, self);
        thread_files_release(self);
    }

    /* close all file descriptors */
    for (int i = 0; i < FDS_COUNT; ++i)
        proc_fd_close(proc, i);

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

    if (proc->cwd)
        vfs_close(proc->cwd);

    proc->cwd = NULL;

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

    /* its name stays with the zombie (ps, /proc show it) */
    kfree(proc->name);

    queue_remove(procs, proc);
    kfree(proc);

    return 0;
}

/** the lowest free descriptor, reserved while the call sets it up (no one
 * else takes it, it isn't open yet): -1 if there's none */
int proc_fd_get(struct proc *proc)
{
    for (int i = 0; i < FDS_COUNT; ++i) {
        if (!proc->fds[i].file) {
            proc->fds[i] = (struct fd) {.file = FD_RESERVED};
            return i;
        }
    }

    return -1;
}

/** a reserved descriptor wasn't set up after all: free again */
void proc_fd_release(struct proc *proc, int fd)
{
    if (fd >= 0 && fd < FDS_COUNT)
        proc->fds[fd] = (struct fd) {0};
}

/** a reserved descriptor refers to `file' (the reference is the
 * descriptor's now) */
void proc_fd_install(struct proc *proc, int fd, struct file *file, int flags)
{
    proc->fds[fd] = (struct fd) {.file = file, .flags = flags};
}

/** the open file `fd' refers to, NULL if it's free or being set up */
struct file *proc_fd_file(struct proc *proc, int fd)
{
    if (fd < 0 || fd >= FDS_COUNT)
        return NULL;

    struct file *file = proc->fds[fd].file;
    return file == FD_RESERVED ? NULL : file;
}

/** the descriptor is free, its reference to the open file dropped (the
 * last one closes the file: what the close says) */
int proc_fd_close(struct proc *proc, int fd)
{
    struct file *file = proc_fd_file(proc, fd);

    if (!file)
        return -EBADF;

    proc->fds[fd] = (struct fd) {0};
    return file_put(file);
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
 * move `proc' into the process group `pgid' of its session
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
 * free `pgrp' if it has no processes left, and its session if that
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
