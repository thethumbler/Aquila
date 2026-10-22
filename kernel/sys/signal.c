/**********************************************************************
 *                            Signals
 *
 * Sending a signal makes it pending (a bit of proc->sig_pending) and, if
 * the process doesn't block it, wakes its threads that sleep where a
 * signal may end the sleep (thread_queue_sleep): they return ERESTARTSYS
 * and their system calls unwind. A signal is delivered on the way back to
 * user mode only, the architecture's return path calling signal_take()
 * (stops, default actions) and running the handler it returns: the
 * kernel never leaves anything halfway for a signal.
 */

#include <core/system.h>
#include <core/arch.h>

#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/kthread.h>
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
 * its own thread on its way back to user mode (signal_take), which sleeps
 * here; signals don't end the sleep, continuing (or killing) does.
 */
void signal_stop(int sig)
{
    struct proc *proc = curproc;

    proc->stopped = sig;
    proc->wait_event = (proc->wait_event & ~WAIT_CONTINUED) | WAIT_STOPPED;
    signal_notify_parent(proc);

    while (proc->stopped)
        thread_queue_sleep_unint(&proc->stop_queue);
}

#define STOP_SIGNALS    ((1UL << SIGSTOP) | (1UL << SIGTSTP) | (1UL << SIGTTIN) | (1UL << SIGTTOU))

static int is_stop_signal(int sig)
{
    return !!(STOP_SIGNALS & (1UL << sig));
}

/* nothing would be done when it's delivered: its handler ignores it, or
 * its default action does (SIGCONT's is done when it's sent) */
static int signal_ignored(struct proc *proc, int sig)
{
    uintptr_t handler = proc->sigaction[sig].sa_handler;

    if (handler == SIG_IGN)
        return 1;

    return handler == SIG_DFL && (sig_default_action[sig] == SIGACT_IGNORE ||
                                  sig_default_action[sig] == SIGACT_CONTINUE);
}

/* make `signal' pending for proc, its sleeping threads woken if it isn't
 * blocked: it's delivered on the way back to user mode */
static int signal_to(struct proc *proc, int signal)
{
    if (signal < 0 || signal > SIG_MAX)
        return -EINVAL;

    /* the kernel's threads (process 0) take none */
    if (proc == kproc)
        return -EPERM;

    /* signal 0 only checks for existence, zombies can't be signalled */
    if (!signal || !proc->running)
        return 0;

    /* a stop signal cancels a pending continue and the other way around,
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

            thread_queue_wakeup(&proc->stop_queue);
        }
    }

    /* ignored when it's sent: discarded (POSIX lets us, blocked or not) */
    if (signal_ignored(proc, signal))
        return 0;

    proc->sig_pending |= 1UL << signal;

    /* blocked: it waits; else the threads sleeping where a signal may end
     * the sleep wake up, to return to user mode and take it */
    if (proc->sigmask & (1UL << signal))
        return 0;

    queue_for (node, &proc->threads) {
        struct thread *thread = node->value;

        if (thread->state == ISLEEP && thread->sleep_node)
            thread_wakeup(thread);
        else
            sched_thread_kick(thread);  /* running elsewhere: it takes it */
    }

    return 0;
}

/* sigaction() set its action: ignored now, it's no longer pending */
void signal_action_set(struct proc *proc, int sig)
{
    if (signal_ignored(proc, sig))
        proc->sig_pending &= ~(1UL << sig);
}

int signal_interrupts(struct thread *thread)
{
    struct proc *proc = thread->owner;

    return proc && proc != kproc && (proc->sig_pending & ~proc->sigmask);
}

void signal_force(struct proc *proc, int signal)
{
    if ((proc->sigmask & (1UL << signal)) || proc->sigaction[signal].sa_handler == SIG_IGN) {
        proc->sigmask &= ~(1UL << signal);
        proc->sigaction[signal].sa_handler = SIG_DFL;
    }

    signal_to(proc, signal);
}

void signal_exit(int sig)
{
    curproc->exit = PROC_EXIT(sig, sig);
    proc_kill(curproc);
    arch_sleep();

    for (;;);
}

int signal_take(void)
{
    struct proc *proc = curproc;

    for (;;) {
        sigset_t ready = proc->sig_pending & ~proc->sigmask;

        if (!proc->running || !ready)
            return 0;

        /* SIGKILL first, then the lowest numbered */
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
                /* the terminal's stops don't stop an orphaned group: no
                 * one would continue it (POSIX); SIGSTOP does */
                if (sig != SIGSTOP && pgrp_orphaned(proc->pgrp, NULL))
                    break;

                signal_stop(sig);
                break;
            default:
                break;
        }
    }
}

int signal_proc_send(struct proc *proc, int signal)
{
    return signal_to(proc, signal);
}

int signal_pgrp_send(struct pgroup *pg, int signal)
{
    queue_for (node, pg->procs)
        signal_to(node->value, signal);

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
