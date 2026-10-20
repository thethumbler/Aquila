#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

/* struct dirent, shared with the kernel */
#include <aquila/dirent.h>

typedef struct {
    int fd;
    struct dirent dirent;   /* last entry returned by readdir() */
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
void rewinddir(DIR *dir);
int closedir(DIR *dir);
int dirfd(DIR *dir);

#endif /* ! _SYS_DIRENT_H */
