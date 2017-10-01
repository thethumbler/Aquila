#ifndef _BITS_DIRENT_H
#define _BITS_DIRENT_H

#include <stdint.h>

#define MAXNAMELEN 256

struct dirent {
    size_t d_ino;   // FIXME 
    char d_name[MAXNAMELEN];
};

typedef struct {
    int fd;
} DIR;

DIR *opendir(const char *fn);
int closedir(DIR *dir);
struct dirent *readdir(DIR *dir);

#endif
