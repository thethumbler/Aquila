/*
 * realpath(): an absolute path without ., .. or symbolic links, resolved
 * one component at a time
 */

#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *realpath(const char *restrict path, char *restrict resolved)
{
    char out[PATH_MAX], rest[PATH_MAX], link[PATH_MAX];
    size_t len;
    int links = 0;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (!*path) {
        errno = ENOENT;
        return NULL;
    }

    if (strlen(path) >= sizeof(rest)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    strcpy(rest, path);

    if (path[0] == '/')
        strcpy(out, "/");
    else if (!getcwd(out, sizeof(out)))
        return NULL;

    char *p = rest;

    while (*p) {
        while (*p == '/')
            ++p;

        if (!*p)
            break;

        char *comp = p;
        p += strcspn(p, "/");

        size_t clen = p - comp;
        int more = *p != '\0';

        if (*p)
            *p++ = '\0';

        if (clen == 1 && comp[0] == '.')
            continue;

        if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
            char *slash = strrchr(out, '/');
            slash[slash == out] = '\0';
            continue;
        }

        len = strlen(out);

        if (len + (len > 1) + clen >= sizeof(out)) {
            errno = ENAMETOOLONG;
            return NULL;
        }

        if (len > 1)
            out[len++] = '/';

        strcpy(out + len, comp);

        struct stat st;

        if (lstat(out, &st))
            return NULL;

        if (S_ISLNK(st.st_mode)) {
            if (++links > SYMLOOP_MAX) {
                errno = ELOOP;
                return NULL;
            }

            ssize_t n = readlink(out, link, sizeof(link) - 1);

            if (n < 0)
                return NULL;

            link[n] = '\0';

            /* continue with the link's contents, then what was left */
            char next[PATH_MAX];
            int len = snprintf(next, sizeof(next), "%s%s%s", link, *p ? "/" : "", p);

            if (len < 0 || (size_t) len >= sizeof(next)) {
                errno = ENAMETOOLONG;
                return NULL;
            }

            strcpy(rest, next);
            p = rest;

            if (link[0] == '/') {
                strcpy(out, "/");
            } else {
                /* drop the link itself */
                char *slash = strrchr(out, '/');
                slash[slash == out] = '\0';
            }
        } else if (more && !S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return NULL;
        }
    }

    if (!resolved && !(resolved = malloc(strlen(out) + 1)))
        return NULL;

    return strcpy(resolved, out);
}
