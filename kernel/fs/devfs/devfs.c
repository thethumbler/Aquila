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
#include <core/module.h>
#include <core/string.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/tmpfs.h>

#include <bits/errno.h>

/* devfs root directory (usually mounted on '/dev') */
struct vnode *devfs_root = NULL;

static int devfs_init()
{
    /* devfs is really just tmpfs */
    devfs.vops = tmpfs.vops;
    devfs.fops = tmpfs.fops;

    devfs_root = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!devfs_root) return -ENOMEM;

    devfs_root->ino   = (vino_t) devfs_root;
    devfs_root->mode  = S_IFDIR | 0775;
    devfs_root->nlink = 2;
    devfs_root->fs    = &devfs;
    devfs_root->ref   = 1;

    struct timespec ts;
    gettime(&ts);

    devfs_root->ctime = ts;
    devfs_root->atime = ts;
    devfs_root->mtime = ts;

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
    .nodev  = 1,
    .init   = devfs_init,
    .mount  = devfs_mount,
};

MODULE_INIT(devfs, devfs_init, NULL)
