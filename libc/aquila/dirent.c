#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <aquila/syscall.h>

#include "syscall.h"

DIR *opendir(const char *path)
{
    int fd = open(path, O_SEARCH | O_DIRECTORY);

    if (fd == -1)
        return NULL;

    DIR *dir = malloc(sizeof(DIR));

    if (!dir) {
        close(fd);
        return NULL;
    }

    dir->fd = fd;
    return dir;
}

struct dirent *readdir(DIR *dir)
{
    /* returns the number of entries read, 0 at the end of the directory */
    long ret = __syscall(SYS_readdir, dir->fd, (long) &dir->dirent, 0);

    if (ret <= 0) {
        if (ret < 0)
            errno = -ret;
        return NULL;
    }

    return &dir->dirent;
}

void rewinddir(DIR *dir)
{
    lseek(dir->fd, 0, SEEK_SET);
}

int closedir(DIR *dir)
{
    int ret = close(dir->fd);
    free(dir);
    return ret;
}

int dirfd(DIR *dir)
{
    return dir->fd;
}
