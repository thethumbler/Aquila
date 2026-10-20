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
    unsigned char d_type;   /* DT_UNKNOWN if the filesystem doesn't say */
};

/* d_type values */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

#endif /* ! _AQUILA_DIRENT_H */
