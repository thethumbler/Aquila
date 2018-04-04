/**********************************************************************
 *                  Device Filesystem (devfs) handler
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
#include <fs/tmpfs.h>

#include <bits/errno.h>

/* devfs root directory (usually mounted on '/dev') */
struct inode *devfs_root = NULL;
struct vnode vdevfs_root;

static int devfs_init()
{
    /* devfs is really just tmpfs */
    devfs.iops = tmpfs.iops;
    devfs.fops = tmpfs.fops;

    if (!(devfs_root = kmalloc(sizeof(struct inode))))
        return -ENOMEM;

    memset(devfs_root, 0, sizeof(struct inode));

    devfs_root->name = "dev";
    devfs_root->id   = (vino_t) devfs_root;
    devfs_root->type = FS_DIR;
    devfs_root->size = 0;
    devfs_root->fs   = &devfs;
    devfs_root->p    = NULL;

    vdevfs_root.super = devfs_root;
    vdevfs_root.id    = (vino_t) devfs_root;
    vdevfs_root.type  = FS_DIR;

    vfs_install(&devfs);

    return 0;
}

static int devfs_mount(const char *dir, int flags, void *data)
{
    if (!devfs_root)
        return -EINVAL;

    return vfs_bind(dir, devfs_root);
}

struct fs devfs = {
    .name   = "devfs",
    .init   = devfs_init,
    .mount  = devfs_mount,
};

MODULE_INIT(devfs, devfs_init, NULL);
