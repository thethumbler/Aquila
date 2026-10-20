#ifndef _FS_PIPE_H
#define _FS_PIPE_H

#include <core/system.h>

struct pipe;

#include <fs/vfs.h>
#include <ds/ringbuf.h>

#define PIPE_BUFLEN 1024

/**
 * \ingroup vfs
 * \brief unix pipe
 */
struct pipe {
    /** the ends, NULL once closed (rw_node: a FIFO opened read-write) */
    struct vnode *r_node;
    struct vnode *w_node;
    struct vnode *rw_node;

    /** readers and writers sleep here */
    struct queue *queue;

    /** the FIFO this is the pipe of, NULL for pipe() */
    struct vnode *fifo;

    /** ring buffer */
    struct ringbuf *ring;
};

extern struct fs pipefs;
int pipefs_pipe(struct file *read, struct file *write);
int pipefs_fifo_open(struct file *file);

#endif /* ! _FS_PIPE_H */
