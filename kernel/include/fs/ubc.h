#ifndef _FS_UBC_H
#define _FS_UBC_H

#include <core/system.h>

#define UBC_LINES   16
#define UBC_ASSOC   4

struct ubc_line {
    uintptr_t tag;  /* Block ID */
    size_t access;  /* Access time */
    char *data;     /* Data pointer */
};

struct ubc {
    /* Block size */
    size_t bs;   
    /* Cache lines */
    struct ubc_line lines[UBC_LINES][UBC_ASSOC];
    /* Callbacks */
    void *p;
    ssize_t (*fill)(void *p, uintptr_t block, void *buf);
    ssize_t (*flush)(void *p, uintptr_t block, void *buf);
    /* access time hack, because we don't have time yet -- dah FIXME */
    size_t time;
};

struct ubc *ubc_new(size_t bs, void *p, void *fill, void *flush);
ssize_t ubc_read(struct ubc *ubc, uintptr_t block, void *buf);
ssize_t ubc_write(struct ubc *ubc, uintptr_t block, void *buf);

#endif /* ! _FS_UBC_H */
