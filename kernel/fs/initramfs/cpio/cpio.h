#ifndef _CPIO_H
#define _CPIO_H

#include <core/system.h>
#include <fs/vfs.h>

#define CPIO_BIN_MAGIC    0070707
#define CPIO_TYPE_MASK    0170000
#define CPIO_TYPE_SOCKET  0140000
#define CPIO_TYPE_SLINK   0120000
#define CPIO_TYPE_RGL     0100000
#define CPIO_TYPE_BLKDEV  0060000
#define CPIO_TYPE_DIR     0040000
#define CPIO_TYPE_CHRDEV  0020000
#define CPIO_TYPE_FIFO    0010000
#define CPIO_FLAG_SUID    0004000
#define CPIO_FLAG_SGID    0002000
#define CPIO_FLAG_STICKY  0001000
#define CPIO_PERM_MASK    0000777

struct __cpio_hdr {
    uint16_t magic;
    uint16_t dev;
    uint16_t ino;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t nlink;
    uint16_t rdev;
    uint16_t mtimes[2];
    uint16_t namesize;
    uint16_t filesize[2];
};

struct __cpio_priv {
    struct inode *super;
    struct inode *parent;
    struct inode *dir;
    size_t count;
    size_t data; /* offset of data in the archive */

    struct inode *next;  /* For directories */
};

extern struct fs __cpio;

#endif /* !_CPIO_H */
