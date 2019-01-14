/*
 *          PS/2 Keyboard driver
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>
#include <core/module.h>
#include <core/arch.h>
#include <cpu/cpu.h>

#include <platform/misc.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/dev.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <ds/ringbuf.h>
#include <ds/queue.h>

#include <bits/errno.h>

#define BUFSIZE    128

static struct ringbuf *kbd_ring = RINGBUF_NEW(BUFSIZE);   /* Keboard Ring Buffer */
static struct proc *proc = NULL; /* Current process using Keboard */
static struct queue *kbd_read_queue = QUEUE_NEW(); /* Keyboard read queue */

static void ps2kbd_handler(int scancode)
{
    ringbuf_write(kbd_ring, sizeof(scancode), (char *) &scancode);
    
    if (kbd_read_queue->count)
        thread_queue_wakeup(kbd_read_queue);
}

static void ps2kbd_register(void)
{
    x86_i8042_handler_register(1, ps2kbd_handler);
}

static ssize_t ps2kbd_read(struct devid *dd, off_t off, size_t sz, void *buf)
{
    (void) dd;
    (void) off;

    return ringbuf_read(kbd_ring, sz, buf);
}

static int ps2kbd_probe(void)
{
    ps2kbd_register();
    kdev_chrdev_register(11, &ps2kbddev);
    return 0;
}

/* File Operations */
static int ps2kbd_file_open(struct file *file)
{
    if (proc) /* Only one process can open kbd */
        return -EACCES;

    proc = cur_thread->owner;

    /* This is either really smart or really dumb */
    file->inode->read_queue = kbd_read_queue;    

    return posix_file_open(file);
}

struct dev ps2kbddev = {
    .name  = "kbddev",

    .probe = ps2kbd_probe,
    .read  = ps2kbd_read,

    .fops  = {
        .open  = ps2kbd_file_open,
        .read  = posix_file_read,

        .can_read  = __vfs_can_always,
        .can_write = __vfs_can_never,
        .eof       = __vfs_eof_never,
    },
};

MODULE_INIT(ps2kbd, ps2kbd_probe, NULL)
