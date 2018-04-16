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

struct inode *ramdisk_dev_node = NULL;
static queue_t *archivers = QUEUE_NEW();

void initramfs_archiver_register(struct fs *fs)
{
    enqueue(archivers, fs);
}

void load_ramdisk()
{
    //printk("[0] Kernel: Ramdisk %p [%d]\n", ramdisk, ramdisk_size);
    printk("kernel: Loading ramdisk\n");

    ramdisk_dev_node = kmalloc(sizeof(struct inode));
    memset(ramdisk_dev_node, 0, sizeof(struct inode));

    extern size_t rd_size;

    *ramdisk_dev_node = (struct inode) {
        .name = "ramdisk",

        .type  = FS_BLKDEV,
        .rdev  = _DEV_T(1, 0),

        .size = rd_size,
    };

    struct inode *root = NULL;
    int err = -1;

    forlinked (node, archivers->head, node->next) {
        struct fs *fs = node->value;

        if (!(err = fs->load(ramdisk_dev_node, &root)))
            break;
    }

    if (err) {
        printk("Error code = %d\n", err);
        panic("Could not load ramdisk\n");
    }

    vfs_mount_root(root);
}
