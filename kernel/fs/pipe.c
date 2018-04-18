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
    struct inode *node = file->node;
    struct pipe *pipe = node->p;
    return size <= ringbuf_available(pipe->ring);
}

static int pipefs_can_write(struct file *file, size_t size)
{
    struct inode *node = file->node;
    struct pipe *pipe = node->p;
    return size >= pipe->ring->size - ringbuf_available(pipe->ring);
}

static int pipefs_mkpipe(struct pipe **pipe)
{
    struct pipe *p = kmalloc(sizeof(struct pipe));

    if (!p)
        return -ENOMEM;

    memset(p, 0, sizeof(struct pipe));
    p->ring = ringbuf_new(PIPE_BUF_LEN);
    
    if (!p->ring) {
        kfree(p);
        return -ENOMEM;
    }

    if (pipe)
        *pipe = p;

    return 0;
}

int pipefs_pipe(struct file *read, struct file *write)
{
    int ret = 0;
    struct pipe *pipe = NULL;

    ret = pipefs_mkpipe(&pipe);

    if (ret)
        return ret;

    read->node = kmalloc(sizeof(struct inode));
    write->node = kmalloc(sizeof(struct inode));

    read->node->read_queue = queue_new();
    write->node->write_queue = read->node->read_queue;

    read->node->fs  = &pipefs;
    write->node->fs = &pipefs;
    read->node->p  = pipe;
    write->node->p = pipe;

    write->flags = O_WRONLY;
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
