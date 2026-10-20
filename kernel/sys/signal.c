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

int signal_proc_send(struct proc *proc, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    /* signal 0 only checks for existence, zombies can't be signalled
     * (their signal queue and threads are already gone) */
    if (!signal || !proc->running)
        return 0;

    /* blocked signals stay pending until unblocked */
    if (proc == curproc && !(proc->sigmask & (1UL << signal))) {
        arch_handle_signal(signal);
    } else {
        enqueue(proc->sig_queue, (void *)(intptr_t) signal);

        /* wake up main thread if sleeping - XXX; only that thread, not
         * everything sleeping on the same queue */
        struct thread *thread = (struct thread *) proc->threads.head->value;
        if (thread->state == ISLEEP)
            thread_wakeup(thread);
    }

    return 0;
}

/**
 * \ingroup sys
 * \brief remove and return the first pending signal that is not blocked,
 * 0 if there is none
 */
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
