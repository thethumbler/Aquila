#ifndef _BITS_DIRENT_H
#define _BITS_DIRENT_H

#include <stdint.h>

#define MAXNAMELEN 256

/**
 * \ingroup vfs
 * \brief directory entry
 */
struct dirent {
    size_t d_ino;   // FIXME 
    char d_name[MAXNAMELEN];
};

#endif
