/** \file
 * Signals: sending them, and taking them on the way back to user mode.
 *
 * Sending a signal makes it pending (a bit of `proc->sig_pending`). If the
 * process doesn't block it, its threads that sleep where a signal may end
 * the sleep (`waitq_sleep()`) wake up: they return `-ERESTARTSYS`, and
 * their system calls unwind. A signal is only delivered on the way back
 * to user mode: the architecture's return path calls `signal_take()`,
 * which carries out stops and default actions, and runs the handler it
 * returns. So the kernel never leaves anything halfway for a signal. See
 * docs/kernel/signals.md.
 */

#include <core/system.h>
#include <core/arch.h>

#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/kthread.h>
#include <sys/sched.h>

#include <aquila/errno.h>

/* What each signal does when it isn't caught (its default action). */
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

/**
 * Says whether delivering a signal to a process would stop it: the signal
 * isn't caught, and its default action is to stop.
 *
 * @param proc  the process
 * @param sig   the signal
 * @return 1 if it would, 0 if not
 */
int signal_stops(struct proc *proc, int sig)
{
    return proc->sigaction[sig].sa_handler == SIG_DFL &&
           sig_default_action[sig] == SIGACT_STOP;
}

/** Tells a process's parent that it stopped or continued: its `wait()`
 * wakes up, and it gets `SIGCHLD` unless it asked not to (`SA_NOCLDSTOP`). */
static void signal_notify_parent(struct proc *proc)
{
    struct proc *parent = proc->parent;

    if (!parent)
        return;

    waitq_wakeup(&parent->wait_queue);

    if (!(parent->sigaction[SIGCHLD].sa_flags & SA_NOCLDSTOP))
        signal_proc_send(parent, SIGCHLD);
}

/**
 * Stops the current process, until `SIGCONT` or `SIGKILL`. Its thread
 * calls this on its way back to user mode (`signal_take()`), and sleeps
 * here; other signals don't end the sleep, only continuing (or killing)
 * the process does.
 *
 * @param sig  the signal that stops it
 */
void signal_stop(int sig)
{
    struct proc *proc = curproc;

    proc->stopped = sig;
    proc->wait_event = (proc->wait_event & ~WAIT_CONTINUED) | WAIT_STOPPED;
    signal_notify_parent(proc);

    while (proc->stopped)
        waitq_sleep_unint(&proc->stop_queue);
}

#define STOP_SIGNALS    ((1UL << SIGSTOP) | (1UL << SIGTSTP) | (1UL << SIGTTIN) | (1UL << SIGTTOU))

/** Returns whether `sig` is one of the stop signals. */
static int is_stop_signal(int sig)
{
    return !!(STOP_SIGNALS & (1UL << sig));
}

/** Returns whether delivering `sig` would do nothing: the process ignores
 * it, or its default action is to ignore it. `SIGCONT` counts, since its
 * action is done when it is sent. */
static int signal_ignored(struct proc *proc, int sig)
{
    uintptr_t handler = proc->sigaction[sig].sa_handler;

    if (handler == SIG_IGN)
        return 1;

    return handler == SIG_DFL && (sig_default_action[sig] == SIGACT_IGNORE ||
                                  sig_default_action[sig] == SIGACT_CONTINUE);
}

/**
 * Sends a signal to a process: makes it pending, and if the process
 * doesn't block it, wakes up its sleeping threads so that one takes it on
 * its way back to user mode. Returns 0, `-EINVAL` for a bad signal, or
 * `-EPERM` for process 0.
 */
static int signal_to(struct proc *proc, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    /* the kernel's threads (process 0) take no signals */
    if (proc == kproc)
        return -EPERM;

    /* signal 0 only checks that the process exists, and zombies can't be
     * signalled */
    if (!signal || !proc->running)
        return 0;

    /* a stop signal cancels a pending SIGCONT and the other way around;
     * SIGCONT (whatever its action) and SIGKILL resume a stopped process */
    if (is_stop_signal(signal)) {
        proc->sig_pending &= ~(1UL << SIGCONT);
    } else if (signal == SIGCONT || signal == SIGKILL) {
        proc->sig_pending &= ~STOP_SIGNALS;

        if (proc->stopped) {
            proc->stopped = 0;

            if (signal == SIGCONT) {
                proc->wait_event = (proc->wait_event & ~WAIT_STOPPED) | WAIT_CONTINUED;
                signal_notify_parent(proc);
            }

            waitq_wakeup(&proc->stop_queue);
        }
    }

    /* a signal that would be ignored is discarded when it's sent, blocked
     * or not (POSIX allows it) */
    if (signal_ignored(proc, signal))
        return 0;

    proc->sig_pending |= 1UL << signal;

    /* a blocked signal waits; otherwise the threads sleeping where a
     * signal may end the sleep wake up, to return to user mode and take it */
    if (proc->sigmask & (1UL << signal))
        return 0;

    queue_for (node, &proc->threads) {
        struct thread *thread = node->value;

        if (thread->state == ISLEEP && thread->waitq)
            thread_wakeup(thread);
        else
            sched_thread_kick(thread);  /* one running on another CPU takes it now */
    }

    return 0;
}

/**
 * Discards a pending signal that `sigaction()` just made ignored.
 *
 * @param proc  the process
 * @param sig   the signal whose action changed
 */
void signal_action_set(struct proc *proc, int sig)
{
    if (signal_ignored(proc, sig))
        proc->sig_pending &= ~(1UL << sig);
}

/**
 * Says whether a thread has a signal to take, which ends a sleep that a
 * signal may end.
 *
 * @param thread  the thread
 * @return 1 if its process has a pending signal that it doesn't block, 0
 *         if not
 */
int signal_interrupts(struct thread *thread)
{
    struct proc *proc = thread->owner;

    return proc && proc != kproc && (proc->sig_pending & ~proc->sigmask);
}

/**
 * Sends a signal that can't be blocked or ignored, such as the `SIGSEGV`
 * of a fault: if the process blocks or ignores it, it is unblocked and its
 * action reset to the default.
 *
 * @param proc    the process
 * @param signal  the signal
 */
void signal_force(struct proc *proc, int signal)
{
    if ((proc->sigmask & (1UL << signal)) || proc->sigaction[signal].sa_handler == SIG_IGN) {
        proc->sigmask &= ~(1UL << signal);
        proc->sigaction[signal].sa_handler = SIG_DFL;
    }

    signal_to(proc, signal);
}

/**
 * Ends the current process because of a signal. It doesn't return.
 *
 * @param sig  the signal, which its parent's `wait()` reports
 */
void signal_exit(int sig)
{
    curproc->exit = PROC_EXIT(sig, sig);
    proc_kill(curproc);
    arch_sleep();

    for (;;);
}

/**
 * Takes the current process's pending signals, on the way back to user
 * mode: ignored ones are dropped, and default actions are carried out
 * (ending or stopping the process), until one with a handler is found.
 *
 * @return a signal whose handler the architecture runs now, or 0 if there
 *         is none
 */
int signal_take(void)
{
    struct proc *proc = curproc;

    for (;;) {
        sigset_t ready = proc->sig_pending & ~proc->sigmask;

        if (!proc->running || !ready)
            return 0;

        /* SIGKILL first, then the lowest numbered signal */
        int sig = (ready & (1UL << SIGKILL)) ? SIGKILL : __builtin_ctzl(ready);
        proc->sig_pending &= ~(1UL << sig);

        uintptr_t handler = proc->sigaction[sig].sa_handler;

        if (handler == SIG_IGN)
            continue;

        if (handler != SIG_DFL)
            return sig;

        switch (sig_default_action[sig]) {
            case SIGACT_ABORT:
            case SIGACT_TERMINATE:
                signal_exit(sig);
            case SIGACT_STOP:
                /* the terminal's stop signals don't stop an orphaned
                 * process group, since no one would continue it (POSIX);
                 * SIGSTOP does */
                if (sig != SIGSTOP && pgrp_orphaned(proc->pgrp, NULL))
                    break;

                signal_stop(sig);
                break;
            default:
                break;
        }
    }
}

/**
 * Sends a signal to a process.
 *
 * @param proc    the process
 * @param signal  the signal (0 only checks that the process exists)
 * @return 0, `-EINVAL` for a bad signal, or `-EPERM` for process 0
 */
int signal_proc_send(struct proc *proc, int signal)
{
    return signal_to(proc, signal);
}

/**
 * Sends a signal to every process of a process group.
 *
 * @param pg      the process group
 * @param signal  the signal
 * @return 0
 */
int signal_pgrp_send(struct pgroup *pg, int signal)
{
    queue_for (node, pg->procs)
        signal_to(node->value, signal);

    return 0;
}

/**
 * Sends a signal as `kill()` does.
 *
 * @param pid     whom to: a process if `pid` > 0, the caller's process
 *                group if 0, every process but init and the caller if -1,
 *                or process group `-pid` if < -1
 * @param signal  the signal
 * @return 0, `-EINVAL` for a bad signal, or `-ESRCH` if no such process
 *         or group exists
 */
int signal_send(pid_t pid, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    if (pid > 0) {
        struct proc *proc = proc_pid_find(pid);

        if (!proc)
            return -ESRCH;

        return signal_to(proc, signal);
    }

    if (pid == -1) {
        int found = 0;

        queue_for (node, procs) {
            struct proc *proc = node->value;

            if (proc->pid == 1 || proc == curproc || proc == kproc)
                continue;

            signal_to(proc, signal);
            found = 1;
        }

        return found ? 0 : -ESRCH;
    }

    pid_t pgid = pid ? -pid : curproc->pgrp->pgid;
    int found = 0;

    queue_for (node, procs) {
        struct proc *proc = node->value;

        if (!proc->running || proc->pgrp->pgid != pgid)
            continue;

        found = 1;
        signal_to(proc, signal);
    }

    return found ? 0 : -ESRCH;
}
