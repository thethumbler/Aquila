#include <core/system.h>

#include <ds/ringbuf.h>
#include <bits/fcntl.h>

#include <fs/pipe.h>
#include <fs/posix.h>

static ssize_t pipefs_read(struct inode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pipe *pipe = node->p;
    return ringbuf_read(pipe->ring, size, buf);
}

static ssize_t pipefs_write(struct inode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pipe *pipe = node->p;
    return ringbuf_write(pipe->ring, size, buf);
}

static int pipefs_can_read(struct file *file, size_t size)
{
    struct inode *node = file->inode;
    struct pipe *pipe = node->p;
    return size <= ringbuf_available(pipe->ring);
}

static int pipefs_can_write(struct file *file, size_t size)
{
    struct inode *node = file->inode;
    struct pipe *pipe = node->p;
    return size >= pipe->ring->size - ringbuf_available(pipe->ring);
}

static int pipefs_mkpipe(struct pipe **ref)
{
    struct pipe *pipe;

    pipe = kmalloc(sizeof(struct pipe));
    if (!pipe) return -ENOMEM;

    memset(pipe, 0, sizeof(struct pipe));
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

    read->inode  = kmalloc(sizeof(struct inode));
    if (!read->inode) {
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->inode = kmalloc(sizeof(struct inode));
    if (!write->inode) {
        kfree(read->inode);
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    read->inode->read_queue   = queue_new();
    if (!read->inode->read_queue) {
        kfree(read->inode);
        kfree(write->inode);
        pipefs_pfree(pipe);
        return -ENOMEM;
    }

    write->inode->write_queue = read->inode->read_queue;

    read->inode->fs  = &pipefs;
    read->inode->p   = pipe;

    write->inode->fs = &pipefs;
    write->inode->p  = pipe;
    write->flags     = O_WRONLY;
    return 0;
}

struct fs pipefs = {
    .name = "pipefs",

    .iops = {
        .read  = pipefs_read,
        .write = pipefs_write,
    },

    .fops = {
        .read      = posix_file_read,
        .write     = posix_file_write,
        .can_read  = pipefs_can_read,
        .can_write = pipefs_can_write,
        .eof       = __vfs_never
    },
};
