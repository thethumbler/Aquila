#include <core/system.h>

#include <ds/ringbuf.h>
#include <bits/fcntl.h>

#include <fs/pipe.h>
#include <fs/posix.h>

MALLOC_DEFINE(M_PIPE, "pipe", "pipe structure");

static ssize_t pipefs_read(struct vnode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pipe *pipe = node->p;
    return ringbuf_read(pipe->ring, size, buf);
}

static ssize_t pipefs_write(struct vnode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pipe *pipe = node->p;
    return ringbuf_write(pipe->ring, size, buf);
}

static int pipefs_can_read(struct file *file, size_t size)
{
    struct vnode *node = file->vnode;
    struct pipe *pipe = node->p;
    return size <= ringbuf_available(pipe->ring);
}

static int pipefs_can_write(struct file *file, size_t size)
{
    struct vnode *node = file->vnode;
    struct pipe *pipe = node->p;
    return size >= pipe->ring->size - ringbuf_available(pipe->ring);
}

static int pipefs_mkpipe(struct pipe **ref)
{
    struct pipe *pipe;

    pipe = kmalloc(sizeof(struct pipe), &M_PIPE, M_ZERO);
    if (!pipe) return -ENOMEM;

    pipe->ring = ringbuf_new(PIPE_BUFLEN);
    
    if (!pipe->ring) {
        kfree(pipe);
        return -ENOMEM;
    }

    if (ref) *ref = pipe;

    return 0;
}

static void pipefs_pfree(struct pipe *pipe)
{
    if (pipe) {
        if (pipe->ring)
            ringbuf_free(pipe->ring);
        kfree(pipe);
    }
}

int pipefs_pipe(struct file *read, struct file *write)
{
    int err = 0;
    struct pipe *pipe = NULL;

    if (!read || !write)
        return -EINVAL;

    err = pipefs_mkpipe(&pipe);
    if (err) return err;

    read->vnode  = kmalloc(sizeof(struct vnode), &M_VNODE, 0);
    if (!read->vnode) {
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->vnode = kmalloc(sizeof(struct vnode), &M_VNODE, 0);
    if (!write->vnode) {
        kfree(read->vnode);
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    read->vnode->read_queue   = queue_new();
    if (!read->vnode->read_queue) {
        kfree(read->vnode);
        kfree(write->vnode);
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->vnode->write_queue = read->vnode->read_queue;

    read->vnode->fs  = &pipefs;
    read->vnode->p   = pipe;

    write->vnode->fs = &pipefs;
    write->vnode->p  = pipe;
    write->flags     = O_WRONLY;
    return 0;
}

struct fs pipefs = {
    .name = "pipefs",

    .vops = {
        .read  = pipefs_read,
        .write = pipefs_write,
    },

    .fops = {
        .read      = posix_file_read,
        .write     = posix_file_write,
        .can_read  = pipefs_can_read,
        .can_write = pipefs_can_write,
        .eof       = __vfs_eof_never
    },
};
