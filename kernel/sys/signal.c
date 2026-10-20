/**********************************************************************
 *                            Signals
 */

#include <core/system.h>
#include <core/arch.h>

#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <aquila/errno.h>

int sig_default_action[SIG_MAX+1] = {
    [SIGABRT] = SIGACT_ABORT,
    [SIGALRM] = SIGACT_TERMINATE,
    [SIGBUS]  = SIGACT_ABORT,
    [SIGCHLD] = SIGACT_IGNORE,
    [SIGCONT] = SIGACT_CONTINUE,
    [SIGEMT]  = SIGACT_ABORT,
    [SIGFPE]  = SIGACT_ABORT,
    [SIGHUP]  = SIGACT_TERMINATE,
    [SIGILL]  = SIGACT_ABORT,
    [SIGINT]  = SIGACT_TERMINATE,
    [SIGKILL] = SIGACT_TERMINATE,
    [SIGPIPE] = SIGACT_TERMINATE,
    [SIGQUIT] = SIGACT_ABORT,
    [SIGSEGV] = SIGACT_ABORT,
    [SIGSTOP] = SIGACT_STOP,
    [SIGTERM] = SIGACT_TERMINATE,
    [SIGTSTP] = SIGACT_STOP,
    [SIGTTIN] = SIGACT_STOP,
    [SIGTTOU] = SIGACT_STOP,
    [SIGUSR1] = SIGACT_TERMINATE,
    [SIGUSR2] = SIGACT_TERMINATE,
    [SIGPOLL] = SIGACT_TERMINATE,
    [SIGPROF] = SIGACT_TERMINATE,
    [SIGSYS]  = SIGACT_ABORT,
    [SIGTRAP] = SIGACT_ABORT,
    [SIGURG]  = SIGACT_IGNORE,
    [SIGWINCH] = SIGACT_IGNORE,
    [SIGVTALRM] = SIGACT_TERMINATE,
    [SIGXCPU] = SIGACT_ABORT,
    [SIGXFSZ] = SIGACT_ABORT,
    [SIGLOST] = SIGACT_TERMINATE,
};

/* whether delivering `sig' to proc stops it (its default action, not caught) */
int signal_stops(struct proc *proc, int sig)
{
    return proc->sigaction[sig].sa_handler == SIG_DFL &&
           sig_default_action[sig] == SIGACT_STOP;
}

/* tell the parent a child stopped or continued */
static void signal_notify_parent(struct proc *proc)
{
    struct proc *parent = proc->parent;

    if (!parent)
        return;

    thread_queue_wakeup(&parent->wait_queue);

    if (!(parent->sigaction[SIGCHLD].sa_flags & SA_NOCLDSTOP))
        signal_proc_send(parent, SIGCHLD);
}

/*
 * Stop the current process by `sig', until SIGCONT or SIGKILL. Called by
 * its own thread, which sleeps here.
 */
void signal_stop(int sig)
{
    struct proc *proc = curproc;

    proc->stopped = sig;
    proc->wait_event = (proc->wait_event & ~WAIT_CONTINUED) | WAIT_STOPPED;
    signal_notify_parent(proc);

    while (proc->stopped)
        thread_queue_sleep(&proc->stop_queue);
}

/* the thread is at a point where it may sleep: take a pending stop */
void signal_stop_pending(void)
{
    int sig = curproc->stop_pending;

    if (sig) {
        curproc->stop_pending = 0;
        signal_stop(sig);
    }
}

static int is_stop_signal(int sig)
{
    return sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU;
}

/* drop the pending signals for which drop() is true */
static void signal_discard(struct proc *proc, int (*drop)(int))
{
    struct qnode *node = proc->sig_queue->head;

    while (node) {
        struct qnode *next = node->next;

        if (drop((int)(intptr_t) node->value))
            queue_node_remove(proc->sig_queue, node);

        node = next;
    }
}

static int is_sigcont(int sig)
{
    return sig == SIGCONT;
}

/* send `signal' to proc, delivered right away if `now' and proc is the
 * current process (and doesn't block it), otherwise queued */
static int signal_to(struct proc *proc, int signal, int now)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    /* signal 0 only checks for existence, zombies can't be signalled
     * (their signal queue and threads are already gone) */
    if (!signal || !proc->running)
        return 0;

    /* a stop signal cancels a pending continue and the other way around,
     * SIGCONT (whatever its action) and SIGKILL resume a stopped process */
    if (is_stop_signal(signal)) {
        signal_discard(proc, is_sigcont);
    } else if (signal == SIGCONT || signal == SIGKILL) {
        signal_discard(proc, is_stop_signal);
        proc->stop_pending = 0;

        if (proc->stopped) {
            proc->stopped = 0;

            if (signal == SIGCONT) {
                proc->wait_event = (proc->wait_event & ~WAIT_STOPPED) | WAIT_CONTINUED;
                signal_notify_parent(proc);
            }

            thread_queue_wakeup(&proc->stop_queue);
        }
    }

    /* blocked signals stay pending until unblocked */
    if (now && proc == curproc && !(proc->sigmask & (1UL << signal))) {
        arch_handle_signal(signal);
    } else {
        enqueue(proc->sig_queue, (void *)(intptr_t) signal);

        /* wake up main thread if sleeping - XXX; only that thread, not
         * everything sleeping on the same queue. A stopped process keeps
         * its signals pending until it is continued */
        struct thread *thread = (struct thread *) proc->threads.head->value;
        if (thread->state == ISLEEP && !proc->stopped)
            thread_wakeup(thread);
    }

    return 0;
}

/**
 * \ingroup sys
 * \brief remove and return the first pending signal that is not blocked,
 * 0 if there is none
 */
int signal_pending(struct proc *proc)
{
    queue_for (node, proc->sig_queue) {
        int signal = (int)(intptr_t) node->value;

        if (!(proc->sigmask & (1UL << signal)))
            return 1;
    }

    return 0;
}

int signal_next(struct proc *proc)
{
    queue_for (node, proc->sig_queue) {
        int signal = (int)(intptr_t) node->value;

        if (!(proc->sigmask & (1UL << signal))) {
            queue_node_remove(proc->sig_queue, node);
            return signal;
        }
    }

    return 0;
}

int signal_proc_send(struct proc *proc, int signal)
{
    return signal_to(proc, signal, 1);
}

/* only queue it, for senders that can't have it delivered right away
 * (drivers, interrupts) */
int signal_proc_post(struct proc *proc, int signal)
{
    return signal_to(proc, signal, 0);
}

int signal_pgrp_post(struct pgroup *pg, int signal)
{
    queue_for (node, pg->procs)
        signal_proc_post(node->value, signal);

    return 0;
}

int signal_pgrp_send(struct pgroup *pg, int signal)
{
    int self = 0;

    /* signals for other processes are only queued, but one for the
     * current process may be handled right away and not return, so it
     * goes last */
    queue_for (node, pg->procs) {
        struct proc *proc = node->value;

        if (proc == curproc)
            self = 1;
        else
            signal_proc_send(proc, signal);
    }

    if (self)
        signal_proc_send(curproc, signal);

    return 0;
}

/**
 * \ingroup sys
 * \brief kill(2): pid > 0 a process, 0 the caller's process group, -1 every
 * process except init and the caller, < -1 the process group -pid
 */
int signal_send(pid_t pid, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    if (pid > 0) {
        struct proc *proc = proc_pid_find(pid);

        if (!proc)
            return -ESRCH;

        return signal_proc_send(proc, signal);
    }

    if (pid == -1) {
        int found = 0;

        queue_for (node, procs) {
            struct proc *proc = node->value;

            if (proc->pid == 1 || proc == curproc)
                continue;

            signal_proc_send(proc, signal);
            found = 1;
        }

        return found ? 0 : -ESRCH;
    }

    pid_t pgid = pid ? -pid : curproc->pgrp->pgid;
    int found = 0, self = 0;

    /* the current process goes last, see signal_pgrp_send() */
    queue_for (node, procs) {
        struct proc *proc = node->value;

        if (!proc->running || proc->pgrp->pgid != pgid)
            continue;

        found = 1;

        if (proc == curproc)
            self = 1;
        else
            signal_proc_send(proc, signal);
    }

    if (self)
        signal_proc_send(curproc, signal);

    return found ? 0 : -ESRCH;
}
