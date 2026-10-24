/** \file
 * Advisory locks on whole files, for `flock()`. A lock is shared or
 * exclusive, and an open file holds it, so the descriptors that `dup()`
 * and `fork()` copied share it. It goes with `LOCK_UN`, or when the open
 * file is closed.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <net/socket.h>
#include <sys/sched.h>
#include <ds/queue.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>

MALLOC_DEFINE(M_FILE_LOCK, "file-lock", "flock() lock");

/** An open file's lock. */
struct file_lock {
    struct vnode *vnode;    /* the file it locks */
    int type;               /* LOCK_SH, LOCK_EX, or 0 while not locked */
};

/* the locks held, and the threads waiting for one to be released */
static struct queue locks = {0};
static struct waitq waiters = {0};

/** Says whether taking a lock of `type` conflicts with a lock that another
 * open file holds on the same file. */
static int lock_conflicts(struct file_lock *lock, int type)
{
    queue_for (node, &locks) {
        struct file_lock *held = node->value;

        if (held != lock && held->vnode == lock->vnode &&
            (type == LOCK_EX || held->type == LOCK_EX))
            return 1;
    }

    return 0;
}

/** Releases a lock, and wakes up the threads waiting for one. */
static void lock_unlock(struct file_lock *lock)
{
    if (!lock->type)
        return;

    queue_remove(&locks, lock);
    lock->type = 0;
    waitq_wakeup(&waiters);
}

/**
 * Takes, changes or releases an open file's lock. Taking a lock waits
 * until no other open file holds a conflicting one, unless `LOCK_NB` is
 * set.
 *
 * @param file  the open file
 * @param op    `LOCK_SH` (shared), `LOCK_EX` (exclusive) or `LOCK_UN`
 *              (release), with `LOCK_NB` not to wait
 * @return 0, `-EWOULDBLOCK` if `LOCK_NB` is set and it would wait,
 *         `-EINVAL`, `-ENOMEM`, or `-ERESTARTSYS` if a signal came while
 *         it waited
 */
int vfs_file_flock(struct file *file, int op)
{
    int type = op & ~LOCK_NB;

    if (type != LOCK_SH && type != LOCK_EX && type != LOCK_UN)
        return -EINVAL;

    if (file->flags & FILE_SOCKET)
        return -EINVAL;

    struct file_lock *lock = file->lock;

    if (type == LOCK_UN) {
        if (lock)
            lock_unlock(lock);
        return 0;
    }

    if (!lock) {
        lock = kmalloc(sizeof(*lock), &M_FILE_LOCK, M_ZERO);
        if (!lock) return -ENOMEM;

        lock->vnode = file->vnode;
        file->lock = lock;
    }

    while (lock_conflicts(lock, type)) {
        if (op & LOCK_NB)
            return -EWOULDBLOCK;

        int err = waitq_sleep(&waiters);

        if (err)
            return err;
    }

    if (!lock->type)
        enqueue(&locks, lock);

    /* changing an exclusive lock to a shared one may let others in */
    if (lock->type == LOCK_EX && type == LOCK_SH)
        waitq_wakeup(&waiters);

    lock->type = type;
    return 0;
}

/**
 * Releases an open file's lock when the file is closed.
 *
 * @param file  the open file
 */
void vfs_file_lock_release(struct file *file)
{
    struct file_lock *lock = file->lock;

    if (!lock)
        return;

    file->lock = NULL;
    lock_unlock(lock);
    kfree(lock);
}
