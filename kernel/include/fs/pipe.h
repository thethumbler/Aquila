#ifndef _FS_PIPE_H
#define _FS_PIPE_H

#include <core/system.h>

struct pipe;

#include <fs/vfs.h>
#include <ds/ringbuf.h>

/* Pipes and FIFOs (fs/pipe.c). */

/* The size of a pipe's buffer. */
#define PIPE_BUFLEN 1024

/** A pipe, or a FIFO's pipe. */
struct pipe {
    /** its ends, NULL once closed (rw_node: a FIFO opened read-write) */
    struct vnode *r_node;
    struct vnode *w_node;
    struct vnode *rw_node;

    /** readers and writers sleep here */
    struct waitq queue;

    /** how many times a FIFO was opened for reading and for writing, so
     *  far: an open waiting for the other side sees that it came, even if
     *  it's gone already */
    unsigned long r_opens;
    unsigned long w_opens;

    /** the FIFO this is the pipe of, or NULL for pipe() */
    struct vnode *fifo;

    /** the data written, not read yet */
    struct ringbuf *ring;
};

/* The pipes' filesystem; makes a pipe's two ends; opens a FIFO. */
extern struct fs pipefs;
int pipefs_pipe(struct file *read, struct file *write);
int pipefs_fifo_open(struct file *file);

#endif /* ! _FS_PIPE_H */
