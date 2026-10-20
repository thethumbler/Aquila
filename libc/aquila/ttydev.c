/*
 * ttyname(): the terminal device in /dev or /dev/pts with the same
 * device number as the descriptor
 */

#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int find_in(const char *dir, dev_t rdev, char *name, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[PATH_MAX];
    struct stat st;
    int ret = ENOTTY;

    if (!d)
        return ENOTTY;

    while ((e = readdir(d))) {
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

        if (stat(path, &st) || !S_ISCHR(st.st_mode) || st.st_rdev != rdev)
            continue;

        ret = strlen(path) < size ? 0 : ERANGE;

        if (!ret)
            strcpy(name, path);

        break;
    }

    closedir(d);
    return ret;
}

int ttyname_r(int fildes, char *name, size_t size)
{
    struct stat st;

    if (fstat(fildes, &st))
        return errno;

    if (!isatty(fildes) || !S_ISCHR(st.st_mode))
        return ENOTTY;

    int ret = find_in("/dev/pts", st.st_rdev, name, size);

    if (ret == ENOTTY)
        ret = find_in("/dev", st.st_rdev, name, size);

    return ret;
}

char *ttyname(int fildes)
{
    static char name[32];
    int ret = ttyname_r(fildes, name, sizeof(name));

    if (ret) {
        errno = ret;
        return NULL;
    }

    return name;
}
