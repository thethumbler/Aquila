#ifndef _PIPE_H
#define _PIPE_H

#include <core/system.h>
#include <fs/vfs.h>
#include <ds/ringbuf.h>

#define PIPE_BUF_LEN    1024

struct pipe {
    unsigned r_ref;   /* Readers reference count */
    unsigned w_ref;   /* Writers reference count */

    struct ringbuf *ring; /* Ring buffer */
};

struct fs pipefs;
int pipefs_pipe(struct file *read, struct file *write);

#endif /* ! _PIPE_H */
