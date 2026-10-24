/** \file
 * System calls about signals, timers and the time: `kill()`,
 * `sigaction()`, the signal mask, `sigsuspend()` and `sigreturn()`,
 * `setitimer()`, `nanosleep()`, and the clocks.
 */

#include "syscalls.h"

/**
 * Implements `kill()`: sends a signal to a process or a process group.
 *
 * @param pid  whom to: a process if > 0, the caller's process group if 0,
 *             every process but init and the caller if -1, or process
 *             group `-pid` if < -1
 * @param sig  the signal, or 0 to only check that the target exists
 * @return 0, `-EINVAL` for a bad signal, or `-ESRCH` if there is no such
 *         process or group
 */
long sys_kill(pid_t pid, int sig)
{
    syscall_log(LOG_DEBUG, "kill(pid=%d, sig=%d)\n", pid, sig);

    /* a signal sent to ourselves is delivered on the way back to user
     * mode, before kill() returns */
    return signal_send(pid, sig);
}

/**
 * Implements `sigaction()`: reads a signal's action, and sets a new one.
 *
 * @param sig   the signal
 * @param act   the new action, or NULL to leave it
 * @param oact  where to put the old action, or NULL
 * @return 0, `-EINVAL` for a bad signal (or catching `SIGKILL` or
 *         `SIGSTOP`), or `-EFAULT`
 */
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

        /* SIGKILL and SIGSTOP can't be caught or ignored */
        if ((sig == SIGKILL || sig == SIGSTOP) && handler != SIG_DFL) {
            return -EINVAL;
        }

        /* a handler must be in user memory */
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

/**
 * Implements `gettimeofday()`: the time of day.
 *
 * @param tv  where to put the time, or NULL
 * @param tz  where to put the time zone, or NULL
 * @return 0, or `-EFAULT`
 */
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

/**
 * Implements `sigprocmask()`: reads and changes the process's signal
 * mask. `SIGKILL` and `SIGSTOP` can't be blocked.
 *
 * @param how     `SIG_BLOCK`, `SIG_UNBLOCK` or `SIG_SETMASK`
 * @param set     the signals, or NULL to leave the mask
 * @param oldset  where to put the old mask, or NULL
 * @return 0, `-EINVAL` for a bad `how`, or `-EFAULT`
 */
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

    /* a pending signal that is no longer blocked is delivered before this
     * returns to user mode */
    curproc->sigmask = mask & ~SIG_UNMASKABLE;
    return 0;
}

/**
 * Implements `sigsuspend()`: replaces the signal mask and waits for a
 * signal. The old mask comes back once a handler has run.
 *
 * @param set  the mask to wait with
 * @return `-EINTR` once a handler has run (it is restarted after a stop),
 *         or `-EFAULT`
 */
long sys_sigsuspend(const sigset_t *set)
{
    syscall_log(LOG_DEBUG, "sigsuspend(set=%p)\n", set);

    sigset_t kset;

    if (copyin(&kset, set, sizeof(kset))) {
        return -EFAULT;
    }

    struct thread *thread = curthread;
    struct waitq wait = {0};

    /* the mask it replaces goes in the handler's frame (sigreturn()
     * restores it), or comes back if the call is restarted */
    thread->sig_oldmask = curproc->sigmask;
    thread->sig_oldmask_set = 1;
    curproc->sigmask = kset & ~SIG_UNMASKABLE;

    /* sleep until a signal it doesn't block comes (nothing else wakes it
     * up) */
    while (!waitq_sleep(&wait))
        ;

    /* EINTR once a handler ran; restarted if none did (a stop) */
    return -ERESTARTNOHAND;
}

/**
 * Implements `sigreturn()`: a signal handler returns, and the registers
 * and signal mask that its frame saved are restored. The kernel's signal
 * code page calls it when a handler returns.
 *
 * @return 0, which `syscall_dispatch()` ignores: the registers are the
 *         frame's
 */
long sys_sigreturn(void)
{
    syscall_log(LOG_DEBUG, "sigreturn()\n");
    arch_sigreturn();

    /* the registers are the frame's now: syscall_dispatch() doesn't set
     * a result */
    return 0;
}

/**
 * Implements `setitimer()`: sets the real time timer, which sends
 * `SIGALRM`, and reads the old one.
 *
 * @param which  `AQUILA_ITIMER_REAL` (the only timer supported)
 * @param new    the new timer, or NULL to only read the old one
 * @param old    where to put the old timer, or NULL
 * @return 0, `-EINVAL` for another timer, or `-EFAULT`
 */
long sys_setitimer(int which, const struct aquila_itimer *new, struct aquila_itimer *old)
{
    syscall_log(LOG_DEBUG, "setitimer(which=%d, new=%p, old=%p)\n", which, new, old);

    struct aquila_itimer knew;

    if (new && copyin(&knew, new, sizeof(knew))) {
        return -EFAULT;
    }

    /* only the real time timer: the kernel doesn't count virtual time */
    if (which != AQUILA_ITIMER_REAL) {
        return -EINVAL;
    }

    struct aquila_itimer prev = {0};

    /* check that the old timer can be written out before setting the new
     * one */
    if (old && copyout(old, &prev, sizeof(prev))) {
        return -EFAULT;
    }

    if (new)
        sched_alarm(curproc, knew.value, knew.interval, &prev.value, &prev.interval);
    else
        sched_alarm_get(curproc, &prev.value, &prev.interval);

    return old ? copyout(old, &prev, sizeof(prev)) : 0;
}

/**
 * Implements `nanosleep()`: sleeps for a time. A stop doesn't change the
 * deadline: the call is restarted until the same time.
 *
 * @param req  how long to sleep
 * @param rem  where to put the time left if a signal ends the sleep, or
 *             NULL
 * @return 0, `-EINTR` if a handler ran, `-EINVAL` for a bad time, or
 *         `-EFAULT`
 */
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

    /* a call restarted after a stop keeps the deadline it had; otherwise
     * the deadline is counted from now */
    uint64_t until = curthread->restarting ? curthread->restart_until :
                     arch_rtime_ns() + kreq.tv_sec * 1000000000ULL + kreq.tv_nsec;

    curthread->restart_until = until;

    if (sched_sleep(until)) {
        /* a signal: write out the time left; the call fails with EINTR
         * once a handler ran, or is restarted after a stop, until the same
         * deadline */
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

/**
 * Implements `clock_gettime()`: reads a clock.
 *
 * @param clock  `CLOCK_REALTIME` or `CLOCK_MONOTONIC`
 * @param ts     where to put the time
 * @return 0, `-EINVAL` for another clock, or `-EFAULT`
 */
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
