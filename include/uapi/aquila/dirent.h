#ifndef _AQUILA_DIRENT_H
#define _AQUILA_DIRENT_H

#include <stddef.h>

#define MAXNAMELEN 256

/**
 * \ingroup vfs
 * \brief directory entry
 */
struct dirent {
    size_t d_ino;
    char d_name[MAXNAMELEN];
};

#endif /* ! _AQUILA_DIRENT_H */
