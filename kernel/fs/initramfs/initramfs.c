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

static struct inode *rd_dev = NULL;
static struct queue *archivers = QUEUE_NEW();

int initramfs_archiver_register(struct fs *fs)
{
    if (!enqueue(archivers, fs))
        return -ENOMEM;

    return 0;
}

int load_ramdisk(module_t *module)
{
    printk("kernel: Loading ramdisk\n");

    rd_dev = kmalloc(sizeof(struct inode), &M_INODE, 0);
    memset(rd_dev, 0, sizeof(struct inode));

    extern size_t rd_size;

    rd_dev->mode = S_IFBLK;
    rd_dev->rdev = DEV(1, 0);
    rd_dev->size = rd_size;

    struct inode *root = NULL;
    int err = -1;

    for (struct qnode *node = archivers->head; node; node = node->next) {
        struct fs *fs = node->value;

        if (!(err = fs->load(rd_dev, &root)))
            break;
    }

    if (err) {
        printk("Error code = %d\n", err);
        panic("Could not load ramdisk\n");
    }

    vfs_mount_root(root);

    return 0;
}
