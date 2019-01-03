#ifndef _BITS_DIRENT_H
#define _BITS_DIRENT_H

#include <stdint.h>

#define MAXNAMELEN 256

struct dirent {
    size_t d_ino;   // FIXME 
    char d_name[MAXNAMELEN];
};

#endif
