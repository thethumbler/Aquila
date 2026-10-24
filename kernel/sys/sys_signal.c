/** \file
 * System calls: signals, timers and the time: kill, sigaction, the
 * masks, sigsuspend and sigreturn, setitimer, sleeping, the clocks
 */

#include "syscalls.h"

long sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);

    /* one sent to ourselves is delivered before this returns to user
     * mode (x86_return_to_user) */
    return signal_send(pid, sig);
}

long sys_sigaction(int sig, const struct aquila_sigaction *act, struct aquila_sigaction *oact)
{
    syscall_log(LOG_DEBUG, "sigaction(sig=%d, act=%p, oact=%p)\n",
            sig, act, oact);

    if (sig < 1 || sig > SIG_MAX) {
        return -EINVAL;
    }

    struct aquila_sigaction new;

    if (act && copyin(&new, act, sizeof(new))) {
        return -EFAULT;
    }

    if (act) {
        uintptr_t handler = new.sa_handler;

        /* SIGKILL and SIGSTOP can not be caught or ignored */
        if ((sig == SIGKILL || sig == SIGSTOP) && handler != SIG_DFL) {
            return -EINVAL;
        }

        /* handlers must live in user space */
        if (handler != SIG_DFL && handler != SIG_IGN && uaccess_check((void *) handler, 1)) {
            return -EFAULT;
        }
    }

    if (oact && copyout(oact, &curproc->sigaction[sig], sizeof(struct aquila_sigaction))) {
        return -EFAULT;
    }

    if (act) {
        curproc->sigaction[sig] = new;
        signal_action_set(curproc, sig);
    }

    return 0;
}

long sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    syscall_log(LOG_DEBUG, "gettimeofday(tv=%p, tz=%p)\n", tv, tz);

    struct timeval ktv;
    struct timezone ktz;
    int err = gettimeofday(&ktv, &ktz);

    if (!err && ((tv && copyout(tv, &ktv, sizeof(ktv))) || (tz && copyout(tz, &ktz, sizeof(ktz)))))
        err = -EFAULT;

    return err;
}

long sys_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
    syscall_log(LOG_DEBUG, "sigmask(how=%d, set=%p, oldset=%p)\n", how, set, oldset);

    sigset_t kset, mask = curproc->sigmask;

    if (set && copyin(&kset, set, sizeof(kset))) {
        return -EFAULT;
    }

    if (set) {
        switch (how) {
            case SIG_BLOCK:   mask |= kset;  break;
            case SIG_UNBLOCK: mask &= ~kset; break;
            case SIG_SETMASK: mask = kset;   break;
            default:
                return -EINVAL;
        }
    }

    if (oldset && copyout(oldset, &curproc->sigmask, sizeof(sigset_t))) {
        return -EFAULT;
    }

    /* one no longer blocked is delivered before this returns to user
     * mode */
    curproc->sigmask = mask & ~SIG_UNMASKABLE;
    return 0;
}

long sys_sigsuspend(const sigset_t *set)
{
    syscall_log(LOG_DEBUG, "sigsuspend(set=%p)\n", set);

    sigset_t kset;

    if (copyin(&kset, set, sizeof(kset))) {
        return -EFAULT;
    }

    struct thread *thread = curthread;
    struct waitq wait = {0};

    /* the mask it replaces is the handler's frame's (sigreturn restores
     * it), or comes back if it's restarted (x86_return_to_user) */
    thread->sig_oldmask = curproc->sigmask;
    thread->sig_oldmask_set = 1;
    curproc->sigmask = kset & ~SIG_UNMASKABLE;

    /* until a signal it doesn't block (nothing else wakes it up) */
    while (!waitq_sleep(&wait))
        ;

    /* EINTR once a handler ran; restarted if none did (a stop) */
    return -ERESTARTNOHAND;
}

/** a handler returns (the kernel's sigcode calls this): its frame back */
long sys_sigreturn(void)
{
    syscall_log(LOG_DEBUG, "sigreturn()\n");
    arch_sigreturn();

    /* the registers are the frame's now: syscall_dispatch() doesn't set
     * a result */
    return 0;
}

long sys_setitimer(int which, const struct aquila_itimer *new, struct aquila_itimer *old)
{
    syscall_log(LOG_DEBUG, "setitimer(which=%d, new=%p, old=%p)\n", which, new, old);

    struct aquila_itimer knew;

    if (new && copyin(&knew, new, sizeof(knew))) {
        return -EFAULT;
    }

    /* only the real time timer, the kernel doesn't count virtual time */
    if (which != AQUILA_ITIMER_REAL) {
        return -EINVAL;
    }

    struct aquila_itimer prev = {0};

    /* where the old one goes: writable, before the new one is set */
    if (old && copyout(old, &prev, sizeof(prev))) {
        return -EFAULT;
    }

    if (new)
        sched_alarm(curproc, knew.value, knew.interval, &prev.value, &prev.interval);
    else
        sched_alarm_get(curproc, &prev.value, &prev.interval);

    return old ? copyout(old, &prev, sizeof(prev)) : 0;
}

long sys_nanosleep(const struct aquila_timespec *req, struct aquila_timespec *rem)
{
    syscall_log(LOG_DEBUG, "nanosleep(req=%p, rem=%p)\n", req, rem);

    struct aquila_timespec kreq;

    if (copyin(&kreq, req, sizeof(kreq))) {
        return -EFAULT;
    }

    if (kreq.tv_nsec >= 1000000000) {
        return -EINVAL;
    }

    /* until the deadline it had when a stop interrupted it, restarted
     * after the stop; else from now */
    uint64_t until = curthread->restarting ? curthread->restart_until :
                     arch_rtime_ns() + kreq.tv_sec * 1000000000ULL + kreq.tv_nsec;

    curthread->restart_until = until;

    if (sched_sleep(until)) {
        /* a signal: the time left, EINTR once a handler ran (a stop: the
         * call is restarted, until the same deadline) */
        if (rem) {
            uint64_t now = arch_rtime_ns();
            uint64_t left = now < until ? until - now : 0;
            struct aquila_timespec krem = {
                .tv_sec  = left / 1000000000ULL,
                .tv_nsec = left % 1000000000ULL,
            };

            if (copyout(rem, &krem, sizeof(krem))) {
                return -EFAULT;
            }
        }

        return -ERESTARTNOHAND;
    }

    return 0;
}

long sys_clock_gettime(int clock, struct aquila_timespec *ts)
{
    syscall_log(LOG_DEBUG, "clock_gettime(clock=%d, ts=%p)\n", clock, ts);

    struct aquila_timespec kts;
    struct timespec now;
    uint64_t ns;

    switch (clock) {
        case CLOCK_REALTIME:
            gettime(&now);
            kts.tv_sec  = now.tv_sec;
            kts.tv_nsec = now.tv_nsec;
            break;
        case CLOCK_MONOTONIC:
            ns = arch_rtime_ns();
            kts.tv_sec  = ns / 1000000000ULL;
            kts.tv_nsec = ns % 1000000000ULL;
            break;
        default:
            return -EINVAL;
    }

    return copyout(ts, &kts, sizeof(kts));
}
