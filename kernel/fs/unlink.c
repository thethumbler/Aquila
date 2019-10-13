#include <core/system.h>
#include <fs/vfs.h>

int vfs_unlink(const char *path, struct uio *uio)
{
    int ret = 0;
    struct vfs_path *p = NULL;
    char **tokens = NULL;

    /* if path is NULL pointer, or path is empty string, return NULL */
    if (!path ||  !*path)
        return -ENOENT;

    char *_path = NULL;
    if ((ret = vfs_parse_path(path, uio, &_path)))
        goto error;

    /* Canonicalize Path */
    tokens = tokenize_path(_path);

    /* Get mountpoint & path */
    p = vfs_get_mountpoint(tokens);

    struct vnode *dir = p->root;
    char *name = NULL;
    char **tok = p->tokens;

    while (tok) {
        char *token = *tok;

        if (!*(tok + 1)) {
            name = token;
            break;
        }

        struct dirent dirent;
        if ((ret = vfs_finddir(dir, token, &dirent)))
            goto error;

        if ((ret = vfs_vget(p->root, dirent.d_ino, &dir)))
            goto error;

        ++tok;
    }

    if ((ret = vfs_vunlink(dir, name, uio)))
        goto error;

    free_tokens(tokens);
    kfree(p);
    kfree(_path);
    return 0;

error:
    if (tokens)
        free_tokens(tokens);
    if (p)
        kfree(p);
    if (_path)
        kfree(_path);
    return ret;
}
