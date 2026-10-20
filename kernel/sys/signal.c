/**********************************************************************
 *                            Signals
 *
 *
 *  This file is part of AquilaOS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) Mohamed Anwar
 */

#include <core/system.h>
#include <core/arch.h>

#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <bits/errno.h>

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
    //[SIGPROF] = SIGACT_TERMINATE,
    [SIGSYS]  = SIGACT_ABORT,
    [SIGTRAP] = SIGACT_ABORT,
    [SIGURG]  = SIGACT_IGNORE,
    [SIGWINCH] = SIGACT_IGNORE,
    //[SIGVTALRM] = SIGACT_TERMINATE,
    //[SIGXCPU] = SIGACT_ABORT,
    //[SIGXFSZ] = SIGACT_ABORT,
};

int signal_proc_send(struct proc *proc, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    /* signal 0 only checks for existence, zombies can't be signalled
     * (their signal queue and threads are already gone) */
    if (!signal || !proc->running)
        return 0;

    if (proc == curproc) {
        arch_handle_signal(signal);
    } else {
        enqueue(proc->sig_queue, (void *)(intptr_t) signal);

        /* wake up main thread if sleeping - XXX */
        struct thread *thread = (struct thread *) proc->threads.head->value;
        if (thread->state == ISLEEP)
            thread_queue_wakeup(thread->sleep_queue);
    }

    return 0;
}

int signal_pgrp_send(struct pgroup *pg, int signal)
{
    queue_for (node, pg->procs) {
        struct proc *proc = node->value;
        signal_proc_send(proc, signal);
    }

    return 0;
}

int signal_send(pid_t pid, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    struct proc *proc = proc_pid_find(pid);

    if (!proc)
        return -ESRCH;

    return signal_proc_send(proc, signal);
}
