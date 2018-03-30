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
#include <core/arch.h>
#include <cpu/cpu.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/dev.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <ds/ring.h>
#include <ds/queue.h>

#include <bits/errno.h>

#define BUF_SIZE    128

static struct ringbuf *kbd_ring = RINGBUF_NEW(BUF_SIZE);   /* Keboard Ring Buffer */
static proc_t *proc = NULL; /* Current process using Keboard */
static queue_t *kbd_read_queue = NEW_QUEUE; /* Keyboard read queue */

void ps2kbd_handler(int scancode)
{
    ringbuf_write(kbd_ring, sizeof(scancode), (char *) &scancode);
    
    if (kbd_read_queue->count)
        thread_queue_wakeup(kbd_read_queue);
}

void ps2kbd_register()
{
    extern void i8042_register_handler(int channel, void (*fun)(int));
    i8042_register_handler(1, ps2kbd_handler);
}

static ssize_t ps2kbd_read(struct devid *dd __unused, off_t offset __unused, size_t size, void *buf)
{
    return ringbuf_read(kbd_ring, size, buf);
}

int ps2kbd_probe()
{
    ps2kbd_register();
    kdev_chrdev_register(11, &ps2kbddev);

    /*
    struct inode *kbd = NULL;
    struct uio uio = {
        .uid  = ROOT_UID,
        .gid  = INPUT_GID,
        .mask = 0660,
    };

    int ret = vfs.create(&vdev_root, "kbd", &uio, &kbd);

    if (ret)
        return ret;

    kbd->type = FS_CHRDEV;
    kbd->dev = &ps2kbddev;
    kbd->read_queue = kbd_read_queue;
    */

    return 0;
}

/* File Operations */

static int ps2kbd_file_open(struct file *file)
{
    if (proc) /* Only one process can open kbd */
        return -EACCES;

    proc = cur_thread->owner;

    /* This is either really smart or really dumb */
    file->node->read_queue = kbd_read_queue;    

    return posix_file_open(file);
}

struct dev ps2kbddev = {
    .name  = "kbddev",

    .probe = ps2kbd_probe,
    .read  = ps2kbd_read,

    .fops  = {
        .open  = ps2kbd_file_open,
        .read  = posix_file_read,

        .can_read  = __always,
        .can_write = __never,
        .eof       = __never,
    },
};

MODULE_INIT(ps2kbd, ps2kbd_probe, NULL);
