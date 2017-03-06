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

int send_signal(pid_t pid, int signal)
{
    if (cur_proc->pid == pid) {
        arch_handle_signal(signal);
        return 0;
    } else {
        proc_t *proc = get_proc_by_pid(pid);

        if (!proc) {
            return -ESRCH;
        } else {
            enqueue(proc->signals_queue, (void *) signal);
            return 0;
        }
    }
    
    return 0;
}
