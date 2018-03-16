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

int send_signal(pid_t pid, int signal)
{
    if (cur_thread->owner->pid == pid) {
        arch_handle_signal(signal);
        return 0;
    } else {
        proc_t *proc = proc_pid_find(pid);

        if (!proc) {
            return -ESRCH;
        } else {
            enqueue(proc->sig_queue, (void *) signal);
            return 0;
        }
    }
    
    return 0;
}
