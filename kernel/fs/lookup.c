#include <core/system.h>
#include <fs/vfs.h>
#include <aquila/fcntl.h>

/* maximum number of symbolic links followed in a lookup (POSIX SYMLOOP_MAX) */
#define SYMLOOP_MAX 8

static int lookup(const char *path, struct uio *uio, struct vnode **ref, char **abs_path, int depth);

/*
 * The path that continues through the symbolic link `link', found as
 * tokens[at] of an absolute path: the directory containing the link
 * (tokens[0..at)) unless the link is absolute, the link's contents, then
 * the components after the link.
 */
static char *link_path(struct vnode *link, char **tokens, size_t at, int *err)
{
    char *target = kmalloc(1024, &M_BUFFER, M_ZERO);
    char *path = NULL;

    if (!target) {
        *err = -ENOMEM;
        return NULL;
    }

    /* leave room for the NUL terminator (buffer is zeroed) */
    ssize_t len = vfs_read(link, 0, 1023, target);

    if (len < 0 || !*target) {
        *err = len < 0 ? len : -ENOENT;
        goto done;
    }

    size_t size = strlen(target) + 2;

    for (size_t i = 0; tokens[i]; ++i)
        size += strlen(tokens[i]) + 1;

    if (!(path = kmalloc(size, &M_BUFFER, 0))) {
        *err = -ENOMEM;
        goto done;
    }

    char *p = path;
    *p = '\0';

    if (target[0] != '/') {
        for (size_t i = 0; i < at; ++i) {
            *p++ = '/';
            p = stpcpy(p, tokens[i]);
        }
    }

    *p++ = '/';
    p = stpcpy(p, target);

    for (size_t i = at + 1; tokens[i]; ++i) {
        *p++ = '/';
        p = stpcpy(p, tokens[i]);
    }

done:
    kfree(target);
    return path;
}

int vfs_lookup(const char *path, struct uio *uio, struct vnode **ref, char **abs_path)
{
    return lookup(path, uio, ref, abs_path, 0);
}

/*
 * Symbolic links are followed wherever they appear in the path, the last
 * component is not followed with O_NOFOLLOW. `abs_path' is the path that
 * was resolved, after following links. `*ref' is only set on success,
 * callers close it on errors.
 */
static int lookup(const char *path, struct uio *uio, struct vnode **ref, char **abs_path, int depth)
{
    vfs_log(LOG_DEBUG, "vfs_lookup(path=%s, uio=%p, ref=%p, abs_path=%p)\n", path, uio, ref, abs_path);

    int ret = 0;
    struct vfs_path *vfs_path = NULL;
    char **tokens = NULL;
    char *next_path = NULL;

    if (!path || !*path)
        return -ENOENT;

    /* get real path (i.e. without . or ..) */
    char *rpath = NULL;
    if ((ret = vfs_parse_path(path, uio, &rpath)))
        goto error;

    tokens = tokenize_path(rpath);

    /* Get mountpoint & path */
    vfs_path = vfs_get_mountpoint(tokens);

    struct vnode *dir = vfs_path->root;
    dir->ref++;

    for (char **token_p = vfs_path->tokens; *token_p; ++token_p) {
        char *token = *token_p;

        struct dirent dirent;
        if ((ret = vfs_finddir(dir, token, &dirent))) {
            vfs_close(dir);
            goto error;
        }

        struct vnode *next = NULL;
        if ((ret = vfs_vget(vfs_path->root, dirent.d_ino, &next))) {
            vfs_close(dir);
            goto error;
        }

        /* release the intermediate directory */
        vfs_close(dir);
        dir = next;

        int last = !token_p[1];

        if (S_ISLNK(dir->mode) && !(last && (uio->flags & O_NOFOLLOW))) {
            if (depth >= SYMLOOP_MAX) {
                vfs_close(dir);
                ret = -ELOOP;
                goto error;
            }

            next_path = link_path(dir, tokens, token_p - tokens, &ret);
            vfs_close(dir);

            if (!next_path)
                goto error;

            ret = lookup(next_path, uio, ref, abs_path, depth + 1);
            goto done;
        }
    }

    if (abs_path) *abs_path = strdup(rpath);

    if (ref)
        *ref = dir;
    else
        vfs_close(dir);

    ret = 0;

done:
error:
    if (next_path) kfree(next_path);
    if (tokens) free_tokens(tokens);
    if (vfs_path) kfree(vfs_path);
    if (rpath) kfree(rpath);

    return ret;
}

/*
 * The directory that contains the last component of `path' (symbolic
 * links followed), and the name of that component. `abs_path' is the
 * absolute path of the component. -EBUSY if path is a mountpoint, which
 * has no entry in a directory of its filesystem.
 */
int vfs_lookup_parent(const char *path, struct uio *uio, struct vnode **dir, char **name, char **abs_path)
{
    int ret = 0;
    char *rpath = NULL, *dname = NULL;
    char **tokens = NULL;
    struct vfs_path *vfs_path = NULL;

    if (!path || !*path)
        return -ENOENT;

    if ((ret = vfs_parse_path(path, uio, &rpath)))
        return ret;

    tokens = tokenize_path(rpath);
    vfs_path = vfs_get_mountpoint(tokens);

    int is_mountpoint = !vfs_path->tokens[0];

    free_tokens(tokens);
    kfree(vfs_path);

    if (is_mountpoint) {
        ret = -EBUSY;
        goto done;
    }

    char *slash = rpath;
    for (char *p = rpath; *p; ++p)
        if (*p == '/')
            slash = p;

    if (!(dname = strdup(slash + 1))) {
        ret = -ENOMEM;
        goto done;
    }

    /* the parent path, "/" for an entry of the root directory */
    char saved = slash[slash == rpath];
    slash[slash == rpath] = '\0';

    struct uio parent_uio = *uio;
    parent_uio.flags &= ~O_NOFOLLOW;

    struct vnode *vnode = NULL;
    ret = lookup(rpath, &parent_uio, &vnode, NULL, 0);
    slash[slash == rpath] = saved;

    if (ret)
        goto done;

    if (!S_ISDIR(vnode->mode)) {
        vfs_close(vnode);
        ret = -ENOTDIR;
        goto done;
    }

    *dir = vnode;
    *name = dname;
    dname = NULL;

    if (abs_path) {
        *abs_path = rpath;
        rpath = NULL;
    }

done:
    if (dname) kfree(dname);
    if (rpath) kfree(rpath);
    return ret;
}
