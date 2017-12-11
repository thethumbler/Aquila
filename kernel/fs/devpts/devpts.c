/**********************************************************************
 *              Pseudo-Terminal device (devpts) handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/devpts.h>
#include <fs/ioctl.h>
#include <fs/termios.h>

#include <ds/ring.h>
#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/sched.h>


/* devpts root directory (usually mounted on /dev/pts) */
struct fs_node *devpts_root = NULL; /* FIXME: should be static */

struct pty {
    int     id;

    struct fs_node *master;
    struct fs_node *slave;

    struct ring *in;    /* Slave Input, Master Output */
    struct ring *out;   /* Slave Output, Master Input */

    char *cook;     /* Cooking buffer */
    size_t pos;

    struct termios tios;

    queue_t pts_read_queue;  /* Slave read, Master write wait queue */
    queue_t pts_write_queue; /* Slave write, Master read wait queue */

    proc_t *proc;   /* Controlling Process */
    proc_t *fg; /* Foreground Process */
};

#define PTY_BUF 512

static struct device ptmdev;
static struct device ptsdev;

static int get_pty_id()
{
    static int id = 0;
    return id++;
}

static struct fs_node *new_ptm(struct pty *pty)
{
    struct fs_node *ptm = kmalloc(sizeof(struct fs_node));
    memset(ptm, 0, sizeof(struct fs_node));

    *ptm = (struct fs_node) {
        .fs = &devfs,
        .dev = &ptmdev,
        .type = FS_PIPE,
        .size = PTY_BUF,
        .p = pty,

        .read_queue  = &pty->pts_read_queue,
        .write_queue = &pty->pts_write_queue,
    };

    return ptm;
}

static struct fs_node *new_pts(struct pty *pty)
{
    char *name = kmalloc(12);
    memset(name, 0, 12);
    snprintf(name, 11, "%d", pty->id);

    struct fs_node *pts = vfs.create(devpts_root, name);

    pts->type = FS_PIPE;
    pts->dev = &ptsdev;
    pts->size = PTY_BUF;
    pts->p = pty;

    pts->read_queue  = &pty->pts_read_queue;
    pts->write_queue = &pty->pts_write_queue;

    return pts;
}

static void new_pty(proc_t *proc, struct fs_node **master)
{
    struct pty *pty = kmalloc(sizeof(struct pty));
    memset(pty, 0, sizeof(struct pty));

    pty->id = get_pty_id();
    pty->in = new_ring(PTY_BUF);
    pty->out = new_ring(PTY_BUF);
    pty->cook = kmalloc(PTY_BUF);
    pty->pos = 0;

    pty->tios.c_lflag |= ICANON | ECHO;

    *master = pty->master = new_ptm(pty);
    
    pty->slave  = new_pts(pty);

    pty->proc = proc;

    printk("[%d] %s: Created ptm/pts pair id=%d\n", proc->pid, proc->name, pty->id);
}

static ssize_t pts_read(struct fs_node *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_read(pty->in, size, buf);
}

static ssize_t ptm_read(struct fs_node *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_read(pty->out, size, buf);  
}

static ssize_t pts_write(struct fs_node *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_write(pty->out, size, buf);
}

static ssize_t ptm_write(struct fs_node *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    ssize_t ret = size;

    /* Process Slave Input */
    if (pty->tios.c_lflag & ICANON) {   /* Canonical mode */
        int echo = pty->tios.c_lflag & ECHO;
        char *c = buf;
        while (size) {

            switch (*c) {
                case '\n':
                    pty->cook[pty->pos++] = *c;
                    if (echo) ring_write(pty->out, 1, "\n");
                    ring_write(pty->in, pty->pos, pty->cook);
                    pty->pos = 0;
                    ret = ret - size + 1;
                    return ret;
                case '\b':
                    if (pty->pos > 0) {
                        --pty->pos;
                        pty->cook[pty->pos] = 0;
                        break;
                    } else {
                        goto skip_echo;
                    }
                default:
                    pty->cook[pty->pos++] = *c;
            }

            if (echo) ring_write(pty->out, 1, c);

skip_echo:
            ++c;
            --size;
        }
    } else {
        return ring_write(pty->in, size, buf);
    }

    return ret;
}

static int ptm_ioctl(struct fs_node *node, unsigned long request, void *argp)
{
    struct pty *pty = (struct pty *) node->p;

    switch (request) {
        case TIOCGPTN:
            *(int *) argp = pty->id;
            break;
        case TIOCSPTLCK:
            *(int *) argp = 0;  /* FIXME */
            break;
        default:
            return -1;
    }
    
    return 0;
}

/* File Operations */
static int ptmx_open(struct file *file)
{   
    new_pty(cur_proc, &(file->node));

    return 0;
}

static struct device ptmxdev = (struct device) {
    .f_ops = {
        .open = ptmx_open,
    },
};

void devpts_init()
{
    devpts.create = devfs.create;
    devpts.find = devfs.find;
    devpts.traverse = devfs.traverse;
    devpts.readdir = devfs.readdir;

    devpts.f_ops.open = devfs.f_ops.open;
    devpts.f_ops.readdir = devfs.f_ops.readdir;

    devpts_root = kmalloc(sizeof(struct fs_node));
    memset(devpts_root, 0, sizeof(struct fs_node));

    *devpts_root = (struct fs_node) {
        .type = FS_DIR,
        .fs   = &devpts,
    };

    struct fs_node *ptmx = vfs.create(dev_root, "ptmx");
    struct fs_node *pts_dir = vfs.mkdir(dev_root, "pts");

    vfs.mount("/dev/pts", devpts_root);

    ptmx->dev = &ptmxdev;
}

static struct device ptsdev = (struct device) {
    .name = "pts",
    .read = pts_read,
    .write = pts_write,

    .f_ops = {
        .open  = generic_file_open,
        .read  = generic_file_read,
        .write = generic_file_write,

        .eof   = __eof_never,
    }
};

static struct device ptmdev = (struct device) {
    .read  = ptm_read,
    .write = ptm_write,
    .ioctl = ptm_ioctl,

    .f_ops = {
        .open  = generic_file_open,
        .read  = generic_file_read,
        .write = generic_file_write,

        .can_read = __can_always,
        .can_write = __can_always,
        .eof = __eof_never,
    },
};

struct fs devpts = {
    .name = "devpts",
};
