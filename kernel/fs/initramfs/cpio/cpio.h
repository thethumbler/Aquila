#ifndef _CPIO_H
#define _CPIO_H

#include <core/system.h>
#include <fs/vfs.h>

#define CPIO_BIN_MAGIC    0070707

#if defined(__TINYC__)
#pragma pack(1)
#endif
struct cpio_hdr {
    uint16_t magic;
    uint16_t dev;
    uint16_t ino;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t nlink;
    uint16_t rdev;
    uint16_t mtime[2];
    uint16_t namesize;
    uint16_t filesize[2];
} __packed;
#if defined(__TINYC__)
#pragma pack()
#endif

struct cpio {
    struct inode *super;
    struct inode *parent;
    struct inode *dir;
    size_t count;
    size_t data; /* offset of data in the archive */

    const char *name;
    struct inode *next;  /* For directories */
};

extern struct fs cpiofs;

#endif /* !_CPIO_H */
