/*
 * Path lookup: vnode by vnode, a component at a time
 *
 * A path is walked from the root (an absolute path) or from the uio's
 * current directory (a relative one; a dirfd's for the *at() calls):
 *
 *   .      the directory itself
 *   ..     its parent, the filesystem's own ".." entry; at the root of a
 *          mounted filesystem, the parent of the directory it's mounted
 *          on; at the root, the root
 *   name   the directory's entry (finddir, vget), the directory
 *          searchable (x) by the uio's user; if a filesystem is mounted
 *          on it, that filesystem's root
 *
 * A symbolic link is followed where it's found (the last component not
 * with O_NOFOLLOW, unless a slash follows it): the walk goes on from the
 * link's directory, with its target, then what was left of the path. A
 * trailing slash wants a directory.
 *
 * Nothing is a string but the path itself: a directory renamed is still
 * the process's current directory, ".." after a symbolic link is the
 * parent of where the link went, and a path is computed only when one
 * is asked for (vfs_vnode_path: getcwd, /proc/mounts).
 */

#include <core/system.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <aquila/fcntl.h>

/* the most symbolic links followed in a lookup (POSIX SYMLOOP_MAX) */
#define SYMLOOP_MAX 8

/* the longest component (POSIX NAME_MAX) */
#define NAME_MAX    255

/* stop at the last component: its directory, and its name */
#define WALK_PARENT 1

static struct vnode *vref(struct vnode *vnode)
{
    vnode->ref++;
    return vnode;
}

/* where absolute paths start */
static struct vnode *walk_root(struct uio *uio)
{
    return uio->root ? uio->root : vfs_root;
}

/* what's mounted on `vnode', all the way down, or itself (the reference
 * passes along) */
static struct vnode *cross_down(struct vnode *vnode)
{
    while (vnode->mounted) {
        struct vnode *root = vref(vnode->mounted);
        vfs_close(vnode);
        vnode = root;
    }

    return vnode;
}

/* `dir''s parent in its filesystem (a reference), NULL at its root */
int vfs_parent(struct vnode *dir, struct vnode **ref)
{
    struct dirent dirent;
    struct vnode *parent = NULL;
    int err;

    *ref = NULL;

    /* a filesystem that doesn't have one: a root */
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

/* `dir''s .. for the walk (a reference): up through mounts, the walk's
 * root's is itself */
static int dotdot(struct vnode *dir, struct uio *uio, struct vnode **ref)
{
    struct vnode *root = walk_root(uio), *d = dir, *parent;
    int err;

    /* the root of a mounted filesystem: where it's mounted */
    while (d != root && d->covered)
        d = d->covered;

    if (d == root) {
        *ref = vref(dir);
        return 0;
    }

    if ((err = vfs_parent(d, &parent)))
        return err;

    /* a filesystem's root, mounted nowhere: as far as it goes */
    *ref = parent ? cross_down(parent) : vref(dir);
    return 0;
}

/* the entry `name' (len bytes) of `dir' (a reference), crossed into what
 * is mounted on it */
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

/* where the symbolic link `link' goes: its target, then `rest' (what's
 * left of the path, its slash first) */
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

static int is_dot(const char *c, size_t len)
{
    return len == 1 && c[0] == '.';
}

static int is_dotdot(const char *c, size_t len)
{
    return len == 2 && c[0] == '.' && c[1] == '.';
}

/*
 * Walk `path' from `dir' (a reference, the walk's now): what it names (a
 * reference), and in *where (if not NULL) the directory its last
 * component was found in (a reference, NULL if it has none); WALK_PARENT,
 * the directory of its last component (a reference) and the component
 * (*name, the caller's to free), -EBUSY if it has none (/), -EINVAL if
 * it's . or ..
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

        /* the component, c (len bytes); whether it's the last, and a
         * slash after it */
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

        /* searching it: its x */
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

            /* on from the link's directory (the root: an absolute one) */
            err = walk(dir, target, uio, flags, links, ref, name, where);
            kfree(target);
            return err;
        }

        /* the last one's directory, kept for the caller */
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

/* where a relative path starts (a reference) */
static struct vnode *walk_start(struct uio *uio)
{
    return vref(uio->cwd ? uio->cwd : walk_root(uio));
}

/**
 * \ingroup vfs
 * \brief the vnode `path' names (a reference, *ref only set on success);
 * a final symbolic link is followed unless uio has O_NOFOLLOW
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
 * \ingroup vfs
 * \brief as vfs_lookup(), and the directory it was found in (a reference,
 * *dir; the vnode itself, referenced again, if the path ends in no name)
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
 * \ingroup vfs
 * \brief the directory (a reference) that has the last component of
 * `path', and that component's name (the caller frees it); -EBUSY if it
 * names a mount point (it has no entry in its directory's filesystem)
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

    /* a mount point: its entry is covered */
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

/* the root of the mounted filesystem `vnode' is in (not a reference:
 * mounts keep theirs) */
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

/* 1 if `vnode' is `dir' or under it, in the same filesystem */
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

/* the name `child' has in `parent' (NAME_MAX + 1 bytes in name), -ENOENT
 * if it has none (it was removed) */
static int entry_name(struct vnode *parent, struct vnode *child, char *name)
{
    struct dirent dirent, found;
    off_t off = 0;
    ssize_t r;

    while ((r = vfs_readdir(parent, off, &dirent)) > 0) {
        off += r;

        if (!strcmp(dirent.d_name, ".") || !strcmp(dirent.d_name, ".."))
            continue;

        /* by its number, as finddir has it (readdir may not say) */
        if (!vfs_finddir(parent, dirent.d_name, &found) && found.d_ino == child->ino) {
            strcpy(name, dirent.d_name);
            return 0;
        }
    }

    return r < 0 ? r : -ENOENT;
}

/**
 * \ingroup vfs
 * \brief the path of `vnode' from `root' (NULL: vfs_root), into buf
 * (size bytes): up through .., each directory's name found in its parent;
 * -ENOENT if a directory on the way was removed, -ERANGE if it doesn't
 * fit
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
        /* the root of a mounted filesystem: the directory it's on */
        while (cur != root && cur->covered) {
            struct vnode *covered = vref(cur->covered);
            vfs_close(cur);
            cur = covered;
        }

        if (cur == root)
            break;

        if ((err = vfs_parent(cur, &parent)))
            break;

        /* a filesystem's root, mounted nowhere */
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

    /* the root itself */
    if (!buf[pos]) {
        if (!pos)
            return -ERANGE;

        buf[--pos] = '/';
    }

    memmove(buf, buf + pos, size - pos);
    return 0;
}
