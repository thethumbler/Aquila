#include <core/system.h>
#include <fs/vfs.h>
#include <bits/fcntl.h>

static inline int vfs_follow(struct vnode *vnode, struct uio *uio, struct vnode **ref)
{
    /* TODO enforce limit */

    int err = 0;
    char *path = NULL;
    
    path = kmalloc(1024, &M_BUFFER, M_ZERO);
    if (!path) goto e_nomem;

    if ((err = vfs_read(vnode, 0, 1024, path)) < 0)
        goto error;

    if ((err = vfs_lookup(path, uio, ref, NULL)))
        goto error;

    kfree(path);
    path = NULL;

    return 0;

e_nomem:
    err = -ENOMEM;

error:
    if (path) kfree(path);
    return err;
}

int vfs_lookup(const char *path, struct uio *uio, struct vnode **ref, char **abs_path)
{
    vfs_log(LOG_DEBUG, "vfs_lookup(path=%s, uio=%p, ref=%p, abs_path=%p)\n", path, uio, ref, abs_path);

    int ret = 0;
    struct vfs_path *vfs_path = NULL;
    char **tokens = NULL;

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
        if ((ret = vfs_finddir(dir, token, &dirent)))
            goto error;

        if ((ret = vfs_vget(vfs_path->root, dirent.d_ino, &dir)))
            goto error;
    }

    free_tokens(tokens);
    kfree(vfs_path);

    if (ref) *ref = dir;
    if (abs_path) *abs_path = strdup(rpath);

    kfree(rpath);

    /* resolve symbolic links */
    if (S_ISLNK(dir->mode) && !(uio->flags & O_NOFOLLOW))
        return vfs_follow(dir, uio, ref);

    return 0;

error:
    if (tokens) free_tokens(tokens);
    if (vfs_path) kfree(vfs_path);
    if (rpath) kfree(rpath);

    return ret;
}

