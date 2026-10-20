#include <core/system.h>
#include <core/time.h>
#include <fs/vfs.h>

/*
 * Changing the mode, owner and times of a vnode. The vnode is changed,
 * then the filesystem stores it (vops.setattr), and it is restored if
 * that fails.
 */

static int store(struct vnode *vnode, struct vnode *saved)
{
    int ret = vfs_setattr(vnode);

    if (ret) {
        vnode->mode  = saved->mode;
        vnode->uid   = saved->uid;
        vnode->gid   = saved->gid;
        vnode->atime = saved->atime;
        vnode->mtime = saved->mtime;
        vnode->ctime = saved->ctime;
    }

    return ret;
}

int vfs_chmod(struct vnode *vnode, mode_t mode, struct uio *uio)
{
    struct vnode saved = *vnode;

    /* only the owner (and root) may change the mode */
    if (uio->uid && uio->uid != vnode->uid)
        return -EPERM;

    mode &= 07777;

    /* setgid is dropped unless the file is in the user's group */
    if (uio->uid && vnode->gid != uio->gid)
        mode &= ~S_ISGID;

    vnode->mode = (vnode->mode & S_IFMT) | mode;
    gettime(&vnode->ctime);

    return store(vnode, &saved);
}

int vfs_chown(struct vnode *vnode, uid_t uid, gid_t gid, struct uio *uio)
{
    struct vnode saved = *vnode;

    /* (uid_t) -1 and (gid_t) -1 leave the owner or group unchanged */
    if (uid == (uid_t) -1)
        uid = vnode->uid;

    if (gid == (gid_t) -1)
        gid = vnode->gid;

    /* others may only change the group of their files to their own group */
    if (uio->uid && (uio->uid != vnode->uid || uid != vnode->uid ||
                     (gid != vnode->gid && gid != uio->gid)))
        return -EPERM;

    /* set-user-id and set-group-id don't carry over to the new owner */
    if (uio->uid && !S_ISDIR(vnode->mode))
        vnode->mode &= ~(S_ISUID | S_ISGID);

    vnode->uid = uid;
    vnode->gid = gid;
    gettime(&vnode->ctime);

    return store(vnode, &saved);
}

/* one time from utimensat(2), `now' for UTIME_NOW */
static void set_time(_time_t *time, const struct aquila_timespec *ts, struct timespec *now)
{
    int32_t nsec = ts ? (int32_t) ts->tv_nsec : UTIME_NOW;

    if (nsec == UTIME_OMIT)
        return;

    if (nsec == UTIME_NOW) {
        *time = *now;
    } else {
        time->tv_sec  = ts->tv_sec;
        time->tv_nsec = ts->tv_nsec;
    }
}

int vfs_utimens(struct vnode *vnode, const struct aquila_timespec times[2], struct uio *uio)
{
    struct vnode saved = *vnode;
    int to_now = 1;

    if (times) {
        for (int i = 0; i < 2; ++i) {
            int32_t nsec = times[i].tv_nsec;

            if (nsec != UTIME_NOW && nsec != UTIME_OMIT) {
                if (nsec < 0 || nsec >= 1000000000)
                    return -EINVAL;

                to_now = 0;
            }
        }
    }

    /* setting the current time needs write permission, other times ownership */
    if (uio->uid && uio->uid != vnode->uid) {
        if (!to_now)
            return -EPERM;

        if (vfs_access(vnode, uio, VFS_W))
            return -EACCES;
    }

    struct timespec now;
    gettime(&now);

    set_time(&vnode->atime, times ? &times[0] : NULL, &now);
    set_time(&vnode->mtime, times ? &times[1] : NULL, &now);
    vnode->ctime = now;

    return store(vnode, &saved);
}
