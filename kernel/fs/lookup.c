/** \file
 * Path lookup: walking a path one component at a time, from vnode to
 * vnode.
 *
 * An absolute path is walked from the root, and a relative one from the
 * uio's current directory (or a directory descriptor's, for the `*at()`
 * calls). Each component is one of:
 *
 *   .      the directory itself;
 *   ..     its parent, the filesystem's own ".." entry; at the root of a
 *          mounted filesystem, the parent of the directory it's mounted
 *          on; at the root, the root itself;
 *   name   the directory's entry (finddir, then vget), if the uio's user
 *          may search the directory (x); if a filesystem is mounted on
 *          the entry, that filesystem's root.
 *
 * A symbolic link is followed where it's found (the last component isn't
 * followed with O_NOFOLLOW, unless a slash comes after it): the walk goes
 * on from the link's directory, with the link's target, then what was
 * left of the path. A trailing slash asks for a directory.
 *
 * Only the path itself is a string: a renamed directory is still a
 * process's current directory, ".." after a symbolic link is the parent
 * of where the link went, and a path is only computed when one is asked
 * for (vfs_vnode_path(), for getcwd() and /proc/mounts).
 */

#include <core/system.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <aquila/fcntl.h>

/* the most symbolic links followed in a lookup (POSIX SYMLOOP_MAX) */
#define SYMLOOP_MAX 8

/* the longest component (POSIX NAME_MAX) */
#define NAME_MAX    255

/* a walk() flag: stop at the last component, with its directory and its
 * name */
#define WALK_PARENT 1

/** Adds a reference to a vnode, and returns it. */
static struct vnode *vref(struct vnode *vnode)
{
    vnode->ref++;
    return vnode;
}

/** Returns where absolute paths start: the process's root directory, or
 * the system's. */
static struct vnode *walk_root(struct uio *uio)
{
    return uio->root ? uio->root : vfs_root;
}

/** Returns the root of what's mounted on `vnode` (all the way down, if a
 * mount is mounted on too), or `vnode` itself. The caller's reference
 * moves to the vnode returned. */
static struct vnode *cross_down(struct vnode *vnode)
{
    while (vnode->mounted) {
        struct vnode *root = vref(vnode->mounted);
        vfs_close(vnode);
        vnode = root;
    }

    return vnode;
}

/**
 * Finds a directory's parent in its own filesystem, through its `..`
 * entry.
 *
 * @param dir  the directory
 * @param ref  set to the parent, with a reference for the caller, or to
 *             NULL at the filesystem's root
 * @return 0, or a negative errno
 */
int vfs_parent(struct vnode *dir, struct vnode **ref)
{
    struct dirent dirent;
    struct vnode *parent = NULL;
    int err;

    *ref = NULL;

    /* no .. entry: it's a root */
    if (vfs_finddir(dir, "..", &dirent))
        return 0;

    if ((err = vfs_vget(dir, dirent.d_ino, &parent)))
        return err;

    /* a root's .. is itself */
    if (parent == dir) {
        vfs_close(parent);
        return 0;
    }

    *ref = parent;
    return 0;
}

/** Finds where `..` goes from `dir` during a walk, up through mounts; the
 * walk's root's `..` is itself. The result in `*ref` has a reference. */
static int dotdot(struct vnode *dir, struct uio *uio, struct vnode **ref)
{
    struct vnode *root = walk_root(uio), *d = dir, *parent;
    int err;

    /* from the root of a mounted filesystem, go to where it's mounted */
    while (d != root && d->covered)
        d = d->covered;

    if (d == root) {
        *ref = vref(dir);
        return 0;
    }

    if ((err = vfs_parent(d, &parent)))
        return err;

    /* a filesystem's root that is mounted nowhere: go no further */
    *ref = parent ? cross_down(parent) : vref(dir);
    return 0;
}

/** Finds the entry `name` (`len` bytes) of `dir`, and crosses into what is
 * mounted on it. The result in `*ref` has a reference. */
static int lookup_name(struct vnode *dir, const char *name, size_t len, struct vnode **ref)
{
    char buf[NAME_MAX + 1];
    struct dirent dirent;
    struct vnode *vnode;
    int err;

    if (len > NAME_MAX)
        return -ENAMETOOLONG;

    memcpy(buf, name, len);
    buf[len] = '\0';

    if ((err = vfs_finddir(dir, buf, &dirent)))
        return err;

    if ((err = vfs_vget(dir, dirent.d_ino, &vnode)))
        return err;

    *ref = cross_down(vnode);
    return 0;
}

/** Returns the path a walk goes on with at a symbolic link: the link's
 * target, then `rest` (what's left of the path, starting with its slash).
 * The caller frees it. On failure it returns NULL, with the error in
 * `*err`. */
static char *link_path(struct vnode *link, const char *rest, int *err)
{
    char *path = kmalloc(1024 + strlen(rest), &M_BUFFER, 0);

    if (!path) {
        *err = -ENOMEM;
        return NULL;
    }

    ssize_t len = vfs_read(link, 0, 1023, path);

    if (len <= 0) {
        *err = len < 0 ? len : -ENOENT;
        kfree(path);
        return NULL;
    }

    path[len] = '\0';
    strcpy(path + len, rest);
    return path;
}

/** Says whether a path component of `len` bytes is a single dot, the
 * name of a directory itself. */
static int is_dot(const char *c, size_t len)
{
    return len == 1 && c[0] == '.';
}

/** Says whether a component (`len` bytes) is `..`. */
static int is_dotdot(const char *c, size_t len)
{
    return len == 2 && c[0] == '.' && c[1] == '.';
}

/**
 * Walks `path` from `dir`, whose reference the walk takes. It returns
 * what the path names in `*ref` (with a reference) and, if `where` isn't
 * NULL, the directory its last component was found in (with a reference,
 * or NULL if there's none).
 *
 * With `WALK_PARENT`, it stops at the last component instead: `*ref` is
 * that component's directory, and `*name` the component (the caller frees
 * it). Then a path with no last component (`/`) is `-EBUSY`, and a last
 * component of `.` or `..` is `-EINVAL`.
 */
static int walk(struct vnode *dir, const char *path, struct uio *uio, int flags, int *links,
                struct vnode **ref, char **name, struct vnode **where)
{
    const char *p = path;
    int err = 0;

    if (*p == '/') {
        vfs_close(dir);
        dir = vref(walk_root(uio));
    }

    for (;;) {
        while (*p == '/')
            ++p;

        if (!*p)
            break;

        /* the component c (len bytes); whether it's the last one, and
         * whether a slash comes after it */
        const char *c = p;
        size_t len = 0;

        while (c[len] && c[len] != '/')
            ++len;

        p += len;

        const char *rest = p;
        while (*rest == '/')
            ++rest;

        int last = !*rest, slash = *p == '/';

        if (!S_ISDIR(dir->mode)) {
            err = -ENOTDIR;
            break;
        }

        if ((flags & WALK_PARENT) && last) {
            if (is_dot(c, len) || is_dotdot(c, len)) {
                err = -EINVAL;
                break;
            }

            if (len > NAME_MAX) {
                err = -ENAMETOOLONG;
                break;
            }

            if (!(*name = kmalloc(len + 1, &M_BUFFER, 0))) {
                err = -ENOMEM;
                break;
            }

            memcpy(*name, c, len);
            (*name)[len] = '\0';

            *ref = dir;
            return 0;
        }

        /* searching the directory needs its x permission */
        if ((err = vfs_access(dir, uio, VFS_X)))
            break;

        struct vnode *next = NULL;

        if (is_dot(c, len))
            next = vref(dir);
        else if (is_dotdot(c, len))
            err = dotdot(dir, uio, &next);
        else
            err = lookup_name(dir, c, len, &next);

        if (err)
            break;

        if (S_ISLNK(next->mode) && (!last || slash || !(uio->flags & O_NOFOLLOW))) {
            if (++*links > SYMLOOP_MAX) {
                vfs_close(next);
                err = -ELOOP;
                break;
            }

            char *target = link_path(next, p, &err);
            vfs_close(next);

            if (!target)
                break;

            /* go on from the link's directory (or from the root, for an
             * absolute target) */
            err = walk(dir, target, uio, flags, links, ref, name, where);
            kfree(target);
            return err;
        }

        /* keep the last component's directory for the caller */
        if (last && where) {
            if (*where)
                vfs_close(*where);
            *where = dir;
        } else {
            vfs_close(dir);
        }

        dir = next;

        if (last && slash && !S_ISDIR(dir->mode)) {
            err = -ENOTDIR;
            break;
        }
    }

    if (!err && (flags & WALK_PARENT))
        err = -EBUSY;

    if (err) {
        vfs_close(dir);
        return err;
    }

    *ref = dir;
    return 0;
}

/** Returns where a relative path starts (with a reference): the current
 * directory. */
static struct vnode *walk_start(struct uio *uio)
{
    return vref(uio->cwd ? uio->cwd : walk_root(uio));
}

/**
 * Finds the vnode a path names. A symbolic link at the end is followed,
 * unless the uio has `O_NOFOLLOW`.
 *
 * @param path  the path
 * @param uio   who is asking, and where relative paths start
 * @param ref   if not NULL, set to the vnode (with a reference for the
 *              caller), on success only
 * @return 0, `-ENOENT`, `-ENOTDIR`, `-EACCES`, `-ELOOP`, or another
 *         negative errno
 */
int vfs_lookup(const char *path, struct uio *uio, struct vnode **ref)
{
    vfs_log(LOG_DEBUG, "vfs_lookup(path=%s, uio=%p, ref=%p)\n", path, uio, ref);

    struct vnode *vnode;
    int links = 0, err;

    if (!path || !*path)
        return -ENOENT;

    if ((err = walk(walk_start(uio), path, uio, 0, &links, &vnode, NULL, NULL)))
        return err;

    if (ref)
        *ref = vnode;
    else
        vfs_close(vnode);

    return 0;
}

/**
 * Finds the vnode a path names, as `vfs_lookup()` does, and the directory
 * it was found in.
 *
 * @param path  the path
 * @param uio   who is asking, and where relative paths start
 * @param ref   set to the vnode (with a reference for the caller)
 * @param dir   set to its directory (with a reference), or to the vnode
 *              again if the path ends with no name (`/`)
 * @return 0, or a negative errno
 */
int vfs_lookup_dir(const char *path, struct uio *uio, struct vnode **ref, struct vnode **dir)
{
    struct vnode *vnode, *where = NULL;
    int links = 0, err;

    if (!path || !*path)
        return -ENOENT;

    if ((err = walk(walk_start(uio), path, uio, 0, &links, &vnode, NULL, &where))) {
        if (where)
            vfs_close(where);
        return err;
    }

    if (!where)
        where = vref(vnode);

    *ref = vnode;
    *dir = where;
    return 0;
}

/**
 * Finds the directory of a path's last component, and the component's
 * name, to make or remove an entry there.
 *
 * @param path  the path
 * @param uio   who is asking, and where relative paths start
 * @param dir   set to the directory (with a reference for the caller)
 * @param name  set to the last component's name (the caller frees it)
 * @return 0, `-EBUSY` if the path names a mount point (or `/`), `-EINVAL`
 *         if it ends with `.` or `..`, or another negative errno
 */
int vfs_lookup_parent(const char *path, struct uio *uio, struct vnode **dir, char **name)
{
    struct vnode *vnode = NULL;
    char *last = NULL;
    int links = 0, err;

    if (!path || !*path)
        return -ENOENT;

    if ((err = walk(walk_start(uio), path, uio, WALK_PARENT, &links, &vnode, &last, NULL)))
        return err;

    /* a mount point: its entry is covered by the mounted filesystem */
    struct dirent dirent;
    struct vnode *entry;

    if (!vfs_finddir(vnode, last, &dirent) && !vfs_vget(vnode, dirent.d_ino, &entry)) {
        int covered = entry->mounted != NULL;
        vfs_close(entry);

        if (covered) {
            vfs_close(vnode);
            kfree(last);
            return -EBUSY;
        }
    }

    *dir  = vnode;
    *name = last;
    return 0;
}

/**
 * Finds the root of the mounted filesystem a vnode is in.
 *
 * @param vnode  the vnode
 * @return the root (without a reference: the mount keeps one)
 */
struct vnode *vfs_mount_root_of(struct vnode *vnode)
{
    struct vnode *cur = vref(vnode), *parent;

    while (cur != vfs_root && !cur->covered && !vfs_parent(cur, &parent) && parent) {
        vfs_close(cur);
        cur = parent;
    }

    vfs_close(cur);
    return cur;
}

/**
 * Says whether a vnode is a directory or under it, in the same
 * filesystem: `rename()` can't move a directory under itself.
 *
 * @param vnode  the vnode
 * @param dir    the directory
 * @return 1 if `vnode` is `dir` or under it, 0 otherwise
 */
int vfs_under(struct vnode *vnode, struct vnode *dir)
{
    struct vnode *cur = vref(vnode), *parent;
    int under = 0;

    for (;;) {
        if (cur == dir) {
            under = 1;
            break;
        }

        if (vfs_parent(cur, &parent) || !parent)
            break;

        vfs_close(cur);
        cur = parent;
    }

    vfs_close(cur);
    return under;
}

/** Finds the name `child` has in `parent` (into `name`, `NAME_MAX` + 1
 * bytes). Returns 0, `-ENOENT` if it has none (it was removed), or a
 * negative errno. */
static int entry_name(struct vnode *parent, struct vnode *child, char *name)
{
    struct dirent dirent, found;
    off_t off = 0;
    ssize_t r;

    while ((r = vfs_readdir(parent, off, &dirent)) > 0) {
        off += r;

        if (!strcmp(dirent.d_name, ".") || !strcmp(dirent.d_name, ".."))
            continue;

        /* compare the entry's inode number as finddir has it (readdir
         * may not fill it in) */
        if (!vfs_finddir(parent, dirent.d_name, &found) && found.d_ino == child->ino) {
            strcpy(name, dirent.d_name);
            return 0;
        }
    }

    return r < 0 ? r : -ENOENT;
}

/**
 * Computes a vnode's path, for `getcwd()` and `/proc/mounts`: it goes up
 * through `..`, and finds each directory's name in its parent.
 *
 * @param vnode  the vnode
 * @param root   where the path starts, or NULL for the system's root
 * @param buf    where the path goes
 * @param size   the buffer's size
 * @return 0, `-ENOENT` if a directory on the way was removed, `-ERANGE`
 *         if the path doesn't fit, or another negative errno
 */
int vfs_vnode_path(struct vnode *vnode, struct vnode *root, char *buf, size_t size)
{
    char name[NAME_MAX + 1];
    struct vnode *cur = vref(vnode), *parent;
    size_t pos = size;
    int err = 0;

    if (!root)
        root = vfs_root;

    if (!size)
        return -ERANGE;

    buf[--pos] = '\0';

    for (;;) {
        /* from the root of a mounted filesystem, go to the directory it's
         * mounted on */
        while (cur != root && cur->covered) {
            struct vnode *covered = vref(cur->covered);
            vfs_close(cur);
            cur = covered;
        }

        if (cur == root)
            break;

        if ((err = vfs_parent(cur, &parent)))
            break;

        /* a filesystem's root that is mounted nowhere: stop */
        if (!parent)
            break;

        if ((err = entry_name(parent, cur, name))) {
            vfs_close(parent);
            break;
        }

        size_t len = strlen(name);

        if (len + 1 > pos) {
            vfs_close(parent);
            err = -ERANGE;
            break;
        }

        pos -= len;
        memcpy(buf + pos, name, len);
        buf[--pos] = '/';

        vfs_close(cur);
        cur = parent;
    }

    vfs_close(cur);

    if (err)
        return err;

    /* the root itself is "/" */
    if (!buf[pos]) {
        if (!pos)
            return -ERANGE;

        buf[--pos] = '/';
    }

    memmove(buf, buf + pos, size - pos);
    return 0;
}
