#include <core/system.h>

#include <ds/ringbuf.h>
#include <aquila/fcntl.h>

#include <fs/pipe.h>
#include <fs/posix.h>
#include <sys/sched.h>
#include <sys/signal.h>

MALLOC_DEFINE(M_PIPE, "pipe", "pipe structure");

/*
 * A pipe has a vnode for each end, the open files of the end reference
 * it; when the last one is closed the end is gone. Anonymous pipes
 * (pipe()) have both ends from the start, a FIFO creates them as it is
 * opened, and a third one for opening it read-write, which is both.
 */

enum { END_READ, END_WRITE, END_RDWR };

static int has_readers(struct pipe *pipe)
{
    return pipe->r_node || pipe->rw_node;
}

static int has_writers(struct pipe *pipe)
{
    return pipe->w_node || pipe->rw_node;
}

static void pipefs_pfree(struct pipe *pipe)
{
    if (pipe) {
        if (pipe->ring)
            ringbuf_free(pipe->ring);
        if (pipe->queue)
            kfree(pipe->queue);
        kfree(pipe);
    }
}

static ssize_t pipefs_read(struct vnode *node, off_t offset __unused, struct iobuf *io)
{
    struct pipe *pipe = node->p;
    return ringbuf_read_iobuf(pipe->ring, io->resid, io);
}

static ssize_t pipefs_write(struct vnode *node, off_t offset __unused, struct iobuf *io)
{
    struct pipe *pipe = node->p;

    /* no one left to read the data */
    if (!has_readers(pipe))
        return -EPIPE;

    return ringbuf_write_iobuf(pipe->ring, io->resid, io);
}

static int pipefs_close(struct vnode *node)
{
    struct pipe *pipe = node->p;

    /* called when the last descriptor of one end is closed */
    if (node == pipe->w_node)
        pipe->w_node = NULL;
    else if (node == pipe->rw_node)
        pipe->rw_node = NULL;
    else
        pipe->r_node = NULL;

    kfree(node);

    if (!has_readers(pipe) && !has_writers(pipe)) {
        /* a FIFO loses its data once no one has it open */
        if (pipe->fifo) {
            pipe->fifo->fifo = NULL;
            vfs_close(pipe->fifo);
        }

        pipefs_pfree(pipe);
        return 0;
    }

    /* wake up the other end so it can see EOF/EPIPE */
    thread_queue_wakeup(pipe->queue);
    return 0;
}

static int pipefs_eof(struct file *file)
{
    struct pipe *pipe = file->vnode->p;

    /* end-of-file once all writers are gone and the pipe is drained */
    return !has_writers(pipe) && !ringbuf_available(pipe->ring);
}

static int pipefs_can_read(struct file *file, size_t size)
{
    struct vnode *node = file->vnode;
    struct pipe *pipe = node->p;

    if (!has_writers(pipe)) /* read will not block, returns EOF */
        return 1;

    return size <= ringbuf_available(pipe->ring);
}

static int pipefs_can_write(struct file *file, size_t size)
{
    struct vnode *node = file->vnode;
    struct pipe *pipe = node->p;
    size_t avail = ringbuf_available(pipe->ring);

    if (!has_readers(pipe)) /* write will not block, returns EPIPE */
        return 1;

    /* ring buffer holds at most size - 1 bytes */
    if (avail >= pipe->ring->size - 1)
        return 0;

    return size <= pipe->ring->size - 1 - avail;
}

static int pipefs_mkpipe(struct pipe **ref)
{
    struct pipe *pipe;

    pipe = kmalloc(sizeof(struct pipe), &M_PIPE, M_ZERO);
    if (!pipe) return -ENOMEM;

    pipe->ring  = ringbuf_new(PIPE_BUFLEN);
    pipe->queue = queue_new();

    if (!pipe->ring || !pipe->queue) {
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    if (ref) *ref = pipe;

    return 0;
}

/* a new reference to an end of pipe (END_*), created if needed */
static struct vnode *pipefs_end(struct pipe *pipe, int which)
{
    struct vnode **end = which == END_READ  ? &pipe->r_node :
                         which == END_WRITE ? &pipe->w_node : &pipe->rw_node;

    if (*end) {
        (*end)->ref++;
        return *end;
    }

    struct vnode *node = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!node)
        return NULL;

    /* readers and writers share a single wait queue: readers sleep on
     * read_queue and wake write_queue, writers do the opposite */
    node->read_queue  = pipe->queue;
    node->write_queue = pipe->queue;

    node->fs   = &pipefs;
    node->p    = pipe;
    node->ref  = 1;
    node->mode = S_IFIFO | 0600;

    *end = node;
    return node;
}

int pipefs_pipe(struct file *read, struct file *write)
{
    int err = 0;
    struct pipe *pipe = NULL;

    if (!read || !write)
        return -EINVAL;

    if ((err = pipefs_mkpipe(&pipe)))
        return err;

    read->vnode = pipefs_end(pipe, END_READ);
    if (!read->vnode) {
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->vnode = pipefs_end(pipe, END_WRITE);
    if (!write->vnode) {
        kfree(read->vnode);
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->flags = O_WRONLY;
    return 0;
}

/*
 * Open the FIFO file->vnode refers to: file->vnode becomes an end of its
 * pipe. Opening for reading waits for a writer and opening for writing
 * for a reader, unless O_NONBLOCK (then a writer without readers fails
 * with ENXIO) or O_RDWR (which is both). On failure file->vnode is left
 * as it was.
 */
int pipefs_fifo_open(struct file *file)
{
    struct vnode *fifo = file->vnode;
    struct pipe *pipe = fifo->fifo;
    int accmode = file->flags & O_ACCMODE;
    int err;

    if (!pipe) {
        if ((err = pipefs_mkpipe(&pipe)))
            return err;

        /* the pipe holds the reference the open came with */
        pipe->fifo = fifo;
        fifo->fifo = pipe;
        fifo->ref++;
    }

    if (accmode == O_WRONLY && (file->flags & O_NONBLOCK) && !has_readers(pipe)) {
        err = -ENXIO;
        goto error;
    }

    int which = accmode == O_RDONLY ? END_READ : accmode == O_WRONLY ? END_WRITE : END_RDWR;
    struct vnode *end = pipefs_end(pipe, which);

    if (!end)
        goto e_nomem;

    /* the other end may be waiting for this one */
    thread_queue_wakeup(pipe->queue);

    if (!(file->flags & O_NONBLOCK)) {
        while (which == END_READ ? !has_writers(pipe) :
               which == END_WRITE ? !has_readers(pipe) : 0) {
            if ((err = thread_queue_sleep(pipe->queue))) {
                /* releases the pipe too if it was the only end */
                vfs_close(end);
                return err;
            }
        }
    }

    vfs_close(fifo);
    file->vnode = end;
    return 0;

e_nomem:
    err = -ENOMEM;
error:
    /* nothing else uses the pipe: drop it */
    if (!has_readers(pipe) && !has_writers(pipe)) {
        fifo->fifo = NULL;
        vfs_close(fifo);
        pipefs_pfree(pipe);
    }

    return err;
}

struct fs pipefs = {
    .name = "pipefs",

    .vops = {
        .read  = pipefs_read,
        .write = pipefs_write,
        .close = pipefs_close,
    },

    .fops = {
        .read      = posix_file_read,
        .write     = posix_file_write,
        .close     = posix_file_close,

        .can_read  = pipefs_can_read,
        .can_write = pipefs_can_write,
        .eof       = pipefs_eof,
    },
};
