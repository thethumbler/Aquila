#ifndef _CPIO_H
#define _CPIO_H

#include <core/system.h>
#include <fs/vfs.h>

/* the magic number of the old binary cpio format */
#define CPIO_BIN_MAGIC    0070707

/** The header of a member of an old binary cpio archive, as it is in the
 * archive: its name follows, then its data. */
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

/** What cpiofs keeps for each of its vnodes, a member of the archive. */
struct cpio {
    struct vnode *super;    /* the archive's device */
    struct vnode *parent;   /* the directory it's in */
    struct vnode *dir;      /* its first entry, if it's a directory */
    size_t count;           /* how many entries it has, if a directory */
    size_t data;            /* where its data starts in the archive */

    const char *name;
    struct vnode *next;     /* the next entry of its directory */
};

extern struct fs cpiofs;

#endif /* !_CPIO_H */
