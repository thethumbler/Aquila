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
#include <bits/errno.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/devpts.h>
#include <fs/ioctl.h>
#include <fs/termios.h>
#include <fs/posix.h>

#include <ds/ring.h>
#include <ds/queue.h>

#include <sys/proc.h>
#include <sys/sched.h>


/* devpts root directory (usually mounted on /dev/pts) */
struct inode *devpts_root = NULL;
struct vnode vdevpts_root;

struct pty {
    int     id;

    struct inode *master;
    struct inode *slave;

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

/*************************************
 *
 * Pseudo Terminal Master Helpers
 *
 *************************************/

static struct inode *new_ptm(struct pty *pty)
{
    struct inode *ptm = kmalloc(sizeof(struct inode));
    memset(ptm, 0, sizeof(struct inode));

    *ptm = (struct inode) { /* Anonymous file, not populated */
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

static ssize_t ptm_read(struct inode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_read(pty->out, size, buf);  
}

static ssize_t ptm_write(struct inode *node, off_t offset __unused, size_t size, void *buf)
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

static int ptm_ioctl(struct inode *node, int request, void *argp)
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

/*************************************
 *
 * Pseudo Terminal Slave Helpers
 *
 *************************************/

static struct inode *new_pts(struct pty *pty)
{
    char *name = kmalloc(12);
    memset(name, 0, 12);
    snprintf(name, 11, "%d", pty->id);

    struct inode *pts = NULL;
    vfs.create(&vdevpts_root, name, 1, 1, 0666, &pts);

    kfree(name);

    pts->type = FS_PIPE;
    pts->dev = &ptsdev;
    pts->size = PTY_BUF;
    pts->p = pty;

    pts->read_queue  = &pty->pts_read_queue;
    pts->write_queue = &pty->pts_write_queue;

    return pts;
}

static ssize_t pts_read(struct inode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_read(pty->in, size, buf);
}

static ssize_t pts_write(struct inode *node, off_t offset __unused, size_t size, void *buf)
{
    struct pty *pty = (struct pty *) node->p;
    return ring_write(pty->out, size, buf);
}

static int pts_ioctl(struct inode *node, int request, void *argp)
{
    printk("pts_ioctl(node=%p, request=%x, argp=%p)\n", node, request, argp);
    struct pty *pty = (struct pty *) node->p;

    switch (request) {
        case TCGETS:
            memcpy(argp, &pty->tios, sizeof(struct termios));
            break;
        case TCSETS:
            memcpy(&pty->tios, argp, sizeof(struct termios));
            break;
        default:
            return -1;
    }
    
    return 0;
}

/*************************************
 *
 * Pseudo Terminal Helpers
 *
 *************************************/

static int get_pty_id()
{
    static int id = 0;
    return id++;
}

static void new_pty(proc_t *proc, struct inode **master)
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

/* File Operations */
static int ptmx_open(struct file *file)
{   
    new_pty(cur_thread->owner, &(file->node));
    return 0;
}

static struct device ptmxdev = (struct device) {
    .fops = {
        .open = ptmx_open,
    },
};

/*************************************
 *
 * devpts Helpers
 *
 *************************************/

int devpts_init()
{
    /* devpts uses devfs handlers for directories and vnodes */
    devpts.iops.create  = devfs.iops.create;
    devpts.iops.readdir = devfs.iops.readdir;
    devpts.iops.vfind   = devfs.iops.vfind;
    devpts.iops.vget    = devfs.iops.vget;

    //devpts.find     = devfs.find;
    //devpts.traverse = devfs.traverse;

    devpts.fops.open    = devfs.fops.open;
    devpts.fops.readdir = devfs.fops.readdir;

    devpts_root = kmalloc(sizeof(struct inode));

    if (!devpts_root)
        return -ENOMEM;

    memset(devpts_root, 0, sizeof(struct inode));

    *devpts_root = (struct inode) {
        .type = FS_DIR,
        .id   = (vino_t) devpts_root,
        .fs   = &devpts,
    };

    vdevpts_root = (struct vnode) {
        .super = devpts_root,
        .id    = (vino_t) devpts_root,
        .type  = FS_DIR,
    };

    struct inode *ptmx = NULL;
    vfs.create(&vdev_root, "ptmx", 1, 1, 0666, &ptmx);
    vfs.mkdir(&vdev_root, "pts", 1, 1, 0666);
    ptmx->dev = &ptmxdev;

    return 0;
}

static struct device ptsdev = (struct device) {
    .name  = "pts",
    .read  = pts_read,
    .write = pts_write,
    .ioctl = pts_ioctl,

    .fops = {
        .open  = generic_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,

        .eof   = __vfs_never,
    }
};

static struct device ptmdev = (struct device) {
    .read  = ptm_read,
    .write = ptm_write,
    .ioctl = ptm_ioctl,

    .fops = {
        .open  = generic_file_open,
        .read  = posix_file_read,
        .write = posix_file_write,

        .can_read  = __vfs_always,
        .can_write = __vfs_always,
        .eof       = __vfs_never,
    },
};

struct fs devpts = {
    .name = "devpts",
    .init = devpts_init,
};
