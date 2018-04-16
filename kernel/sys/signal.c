/**********************************************************************
 *                            Signals
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/arch.h>

#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/sched.h>

#include <bits/errno.h>

int sig_default_action[] = {
    [SIGABRT] = SIGACT_ABORT,
    [SIGALRM] = SIGACT_TERMINATE,
    [SIGBUS]  = SIGACT_ABORT,
    [SIGCHLD] = SIGACT_IGNORE,
    [SIGCONT] = SIGACT_CONTINUE,
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
    //[SIGVTALRM] = SIGACT_TERMINATE,
    //[SIGXCPU] = SIGACT_ABORT,
    //[SIGXFSZ] = SIGACT_ABORT,
};

int signal_proc_send(proc_t *proc, int signal)
{
    printk("signal_proc_send(proc=%p, signal=%d)\n", proc, signal);
    if (proc == cur_thread->owner) {
        arch_handle_signal(signal);
    } else {
        enqueue(proc->sig_queue, (void *) signal);
    }

    return 0;
}

int signal_pgrp_send(pgroup_t *pg, int signal)
{
    printk("signal_pgrp_send(pg=%p, signal=%d)\n", pg, signal);
    forlinked (node, pg->procs->head, node->next) {
        proc_t *proc = node->value;
        signal_proc_send(proc, signal);
    }

    return 0;
}

int signal_send(pid_t pid, int signal)
{
    //printk("send_signal(pid=%d, signal=%d)\n", pid, signal);
    if (cur_thread->owner->pid == pid) {
        arch_handle_signal(signal);
        return 0;
    } else {
        proc_t *proc = proc_pid_find(pid);

        if (!proc)
            return -ESRCH;
        else
            return signal_proc_send(proc, signal);
    }
    
    return 0;
}

