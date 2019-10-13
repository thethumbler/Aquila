/*
 *          VFS => File System binding for CPIO archives
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <mm/mm.h>

#include <dev/ramdev.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <bits/errno.h>

#include <ds/queue.h>
#include <boot/boot.h>

static struct vnode *rd_dev = NULL;
static struct queue *archivers = QUEUE_NEW();

int initramfs_archiver_register(struct fs *fs)
{
    if (!enqueue(archivers, fs))
        return -ENOMEM;

    printk("initramfs: registered archiver: %s\n", fs->name);

    return 0;
}

int load_ramdisk(module_t *module)
{
    printk("kernel: loading ramdisk\n");

    rd_dev = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!rd_dev) return -ENOMEM;

    extern size_t rd_size;

    rd_dev->mode = S_IFBLK;
    rd_dev->rdev = DEV(1, 0);
    rd_dev->size = rd_size;
    rd_dev->fs   = &devfs;

    struct vnode *root = NULL;
    int err = -1;

    queue_for (node, archivers) {
        struct fs *fs = node->value;

        if (!(err = fs->load(rd_dev, &root)))
            break;
    }

    if (err) {
        printk("Error code = %d\n", err);
        panic("Could not load ramdisk\n");
    }

    return vfs_mount_root(root);
}
