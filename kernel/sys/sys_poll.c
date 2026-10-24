/** \file
 * System calls: select() and poll(): waiting for any of several
 * descriptors
 */

#include "syscalls.h"

/** the same, not held: only until the call sleeps (select, poll) */
static struct file *fd_peek(int fd)
{
    return proc_fd_file(curproc, fd);
}

typedef unsigned long fd_mask;

#define  FD_SETSIZE  64
#define   NFDBITS (sizeof (fd_mask) * 8)  /* bits per mask */
#define  _howmany(x,y)   (((x)+((y)-1))/(y))

typedef struct {
    fd_mask fds_bits[_howmany(FD_SETSIZE, NFDBITS)];
} fd_set;

/** whether `file' can be read (or written) without waiting; a file
 * that can't tell is ready, the read or write reports the error */
static int fd_ready(struct file *file, int write)
{
    int ret = write ? vfs_file_can_write(file, 1) : vfs_file_can_read(file, 1);
    return ret != 0;
}

/** select() and poll() wait for `file' (to be read, or written): on the
 * queue its reader (writer) sleeps on, NULL if it can't tell */
static int fd_watch(struct file *file, int write)
{
    return poll_register(vfs_file_wait_queue(file, write));
}

/** deadline of a select()/poll() timeout of `ns' nanoseconds, -1 for
 * none; 0 means don't wait. Restarted after a stop: the deadline it had */
static uint64_t poll_deadline(int64_t ns)
{
    uint64_t until = ns < 0 ? 0 : curthread->restarting ? curthread->restart_until :
                     arch_rtime_ns() + ns;

    curthread->restart_until = until;
    return until;
}

long sys_select(struct aquila_select_args *args)
{
    syscall_log(LOG_DEBUG, "select(args=%p)\n", args);

    struct aquila_select_args a;

    if (copyin(&a, args, sizeof(a))) {
        return -EFAULT;
    }

    int nfds = a.nfds;
    fd_set *readfds = a.readfds;
    fd_set *writefds = a.writefds;
    fd_set *exceptfds = a.exceptfds;
    sigset_t *sigmask = a.sigmask;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        return -EINVAL;
    }

    /* what was asked (the sets are overwritten with the result), the
     * kernel's copies */
    fd_set want_read = {0}, want_write = {0}, out_read, out_write;
    struct timeval timeout;
    sigset_t mask;

    if ((readfds && copyin(&want_read, readfds, sizeof(fd_set))) ||
        (writefds && copyin(&want_write, writefds, sizeof(fd_set))) ||
        (a.timeout && copyin(&timeout, a.timeout, sizeof(timeout))) ||
        (sigmask && copyin(&mask, sigmask, sizeof(mask)))) {
        return -EFAULT;
    }

    if (a.timeout && ((int64_t) timeout.tv_sec < 0 || timeout.tv_usec < 0 || timeout.tv_usec >= 1000000)) {
        return -EINVAL;
    }

    /* pselect(): the mask while waiting; the old one is back when this
     * returns, or in the handler's frame (as in sigsuspend) */
    if (sigmask) {
        curthread->sig_oldmask = curproc->sigmask;
        curthread->sig_oldmask_set = 1;
        curproc->sigmask = mask & ~SIG_UNMASKABLE;
    }

    int64_t ns = a.timeout ? (int64_t) timeout.tv_sec * 1000000000 + timeout.tv_usec * 1000 : -1;
    uint64_t until = poll_deadline(ns);
    int count, timed_out = 0;

#define FD_BIT(set, i) ((set).fds_bits[(i) / NFDBITS] & (1UL << ((i) % NFDBITS)))
#define FD_ADD(set, i) ((set).fds_bits[(i) / NFDBITS] |= (1UL << ((i) % NFDBITS)))

    for (;;) {
        out_read  = (fd_set) {0};
        out_write = (fd_set) {0};
        count = 0;

        for (int i = 0; i < nfds; ++i) {
            if (!FD_BIT(want_read, i) && !FD_BIT(want_write, i))
                continue;

            struct file *file = fd_peek(i);
            int err = 0;

            if (!file)
                err = -EBADF;

            if (!err && FD_BIT(want_read, i) && !(err = fd_watch(file, 0)) && fd_ready(file, 0)) {
                FD_ADD(out_read, i);
                ++count;
            }

            if (!err && FD_BIT(want_write, i) && !(err = fd_watch(file, 1)) && fd_ready(file, 1)) {
                FD_ADD(out_write, i);
                ++count;
            }

            if (err) {
                poll_unregister(curthread);
                return err;
            }
        }

        if (count || ns == 0 || timed_out) {
            poll_unregister(curthread);
            break;
        }

        /* a wakeup of what one of them waits on makes us look again; a
         * signal ends the wait: EINTR (POSIX: not restarted, but after a
         * stop) */
        timed_out = sched_wait(&curthread->poll_wait, until);
        poll_unregister(curthread);

        if (timed_out < 0) {
            return -ERESTARTNOHAND;
        }
    }

    if (sigmask) {
        curproc->sigmask = curthread->sig_oldmask;
        curthread->sig_oldmask_set = 0;
    }

#undef FD_BIT
#undef FD_ADD

    fd_set none = {0};

    if ((readfds && copyout(readfds, &out_read, sizeof(fd_set))) ||
        (writefds && copyout(writefds, &out_write, sizeof(fd_set))) ||
        (exceptfds && copyout(exceptfds, &none, sizeof(fd_set)))) {
        return -EFAULT;
    }

    return count;
}

long sys_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    syscall_log(LOG_DEBUG, "poll(fds=%p, nfds=%d, timeout=%d)\n", fds, nfds, timeout_ms);

    if (nfds > FDS_COUNT * 2) {
        return -EINVAL;
    }

    /* the kernel's copy, the events in it, the results out at the end */
    size_t size = nfds * sizeof(struct pollfd);
    struct pollfd *kfds = nfds ? kmalloc(size, &M_BUFFER, 0) : NULL;

    if (nfds && !kfds) {
        return -ENOMEM;
    }

    if (copyin(kfds, fds, size)) {
        kfree(kfds);
        return -EFAULT;
    }

    int64_t ns = timeout_ms < 0 ? -1 : (int64_t) timeout_ms * 1000000;
    uint64_t until = poll_deadline(ns);
    int count, timed_out = 0;

    for (;;) {
        int err = 0;
        count = 0;

        for (nfds_t i = 0; i < nfds; ++i) {
            struct pollfd *p = &kfds[i];
            struct file *file;

            p->revents = 0;

            /* negative descriptors are skipped */
            if (p->fd < 0)
                continue;

            if (!(file = fd_peek(p->fd))) {
                p->revents = POLLNVAL;
            } else {
                if ((p->events & POLLIN) && !err && !(err = fd_watch(file, 0)) && fd_ready(file, 0))
                    p->revents |= POLLIN;

                if ((p->events & POLLOUT) && !err && !(err = fd_watch(file, 1)) && fd_ready(file, 1))
                    p->revents |= POLLOUT;
            }

            count += !!p->revents;
        }

        if (err) {
            poll_unregister(curthread);
            count = err;
            break;
        }

        if (count || ns == 0 || timed_out) {
            poll_unregister(curthread);
            break;
        }

        /* a wakeup of what one of them waits on makes us look again; a
         * signal ends the wait: EINTR (but after a stop) */
        timed_out = sched_wait(&curthread->poll_wait, until);
        poll_unregister(curthread);

        if (timed_out < 0) {
            count = -ERESTARTNOHAND;
            break;
        }
    }

    if (count >= 0 && copyout(fds, kfds, size))
        count = -EFAULT;

    kfree(kfds);
    return count;
}
