#ifndef _FS_PIPE_H
#define _FS_PIPE_H

#include <core/system.h>

struct pipe;

#include <fs/vfs.h>
#include <ds/ringbuf.h>

#define PIPE_BUFLEN 1024

struct pipe {
    /* Readers reference count */
    unsigned r_ref;

    /* Writers reference count */
    unsigned w_ref;

    struct ringbuf *ring;
};

extern struct fs pipefs;
int pipefs_pipe(struct file *read, struct file *write);

#endif /* ! _FS_PIPE_H */
