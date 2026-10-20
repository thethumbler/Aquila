/*
 * Advisory locks on whole files (flock): shared or exclusive, held by an
 * open file and the descriptors copied from it (dup, fork). They go away
 * with LOCK_UN, or when the last of those descriptors is closed.
 */

#include <core/system.h>
#include <fs/vfs.h>
#include <net/socket.h>
#include <sys/sched.h>
#include <ds/queue.h>

#include <aquila/fcntl.h>
#include <aquila/errno.h>

MALLOC_DEFINE(M_FILE_LOCK, "file-lock", "flock() lock");

struct file_lock {
    struct vnode *vnode;
    int type;   /* LOCK_SH, LOCK_EX, or 0 while not locked */
    int ref;    /* descriptors sharing it */
};

/* locks held, and the threads waiting for one to be released */
static struct queue locks = {0};
static struct queue waiters = {0};

/* whether a lock of the type conflicts with one another file holds */
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

static void lock_unlock(struct file_lock *lock)
{
    if (!lock->type)
        return;

    queue_remove(&locks, lock);
    lock->type = 0;
    thread_queue_wakeup(&waiters);
}

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
        lock->ref = 1;
        file->lock = lock;
    }

    while (lock_conflicts(lock, type)) {
        if (op & LOCK_NB)
            return -EWOULDBLOCK;

        if (thread_queue_sleep(&waiters))
            return -EINTR;
    }

    if (!lock->type)
        enqueue(&locks, lock);

    /* a shared lock instead of an exclusive one may let others in */
    if (lock->type == LOCK_EX && type == LOCK_SH)
        thread_queue_wakeup(&waiters);

    lock->type = type;
    return 0;
}

/*
 * A descriptor is copied (dup, fork): both share a lock. Regular files
 * get one already, so that one taken later through either is the same.
 */
void vfs_file_lock_copy(struct file *file, struct file *copy)
{
    if (!file->lock && !(file->flags & FILE_SOCKET) && S_ISREG(file->vnode->mode)) {
        file->lock = kmalloc(sizeof(struct file_lock), &M_FILE_LOCK, M_ZERO);

        if (file->lock) {
            file->lock->vnode = file->vnode;
            file->lock->ref = 1;
        }
    }

    copy->lock = file->lock;

    if (file->lock)
        file->lock->ref++;
}

/* a descriptor is closed */
void vfs_file_lock_release(struct file *file)
{
    struct file_lock *lock = file->lock;

    if (!lock)
        return;

    file->lock = NULL;

    if (--lock->ref)
        return;

    lock_unlock(lock);
    kfree(lock);
}
