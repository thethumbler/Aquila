/**********************************************************************
 *              Temporary Filesystem (tmpfs) handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>

#include <mm/mm.h>

#include <fs/vfs.h>
#include <fs/virtfs.h>
#include <fs/tmpfs.h>
#include <fs/posix.h>

#include <bits/errno.h>
#include <bits/dirent.h>

static int tmpfs_vget(struct vnode *vnode, struct inode **inode)
{
    printk("tmpfs_vget(vnode={id=%p, type=%d}, inode=%p)\n", vnode->id, vnode->type, inode);

    if (!vnode)
        return -EINVAL;

    /* Inode is always present in memory */
    struct inode *node = (struct inode *) vnode->id;
    node->ref++;

    if (inode)
        *inode = node;

    return 0;
}

static int tmpfs_close(struct inode *inode __unused)
{
    return 0;
}

static ssize_t tmpfs_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    if (!node->size)
        return 0;

    ssize_t r = MIN(node->size - offset, size);
    memcpy(buf, (char *) node->p + offset, r);
    return r;
}

static ssize_t tmpfs_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    if (!node->size) {
        size_t sz = (size_t) offset + size;
        node->p = kmalloc(sz);
        node->size = sz;
    }

    if (((size_t) offset + size) > node->size) {    /* Reallocate */
        size_t sz = (size_t) offset + size;
        void *new = kmalloc(sz);
        memcpy(new, node->p, node->size);
        kfree(node->p);
        node->p = new;
        node->size = sz;
    }

    memcpy((char *) node->p + offset, buf, size);
    return size;
}

/* ================ File Operations ================ */

static int tmpfs_file_open(struct file *file __unused)
{
    return 0;
}

static int tmpfs_file_can_read(struct file *file, size_t size)
{
    if ((size_t) file->offset + size < file->node->size)
        return 1;

    return 0;
}

static int tmpfs_file_can_write(struct file *file __unused, size_t size __unused)
{
    /* TODO impose limit */
    return 1;
}

static int tmpfs_file_eof(struct file *file)
{
    return (size_t) file->offset == file->node->size;
}

static int tmpfs_init()
{
    return 0;
}

static int tmpfs_mount(const char *dir, int flags __unused, void *data __unused)
{
    printk("tmpfs_mount(dir=%s, flags=%x, data=%p)\n", dir, flags, data);

    /* Initalize new */
    struct inode *tmpfs_root = kmalloc(sizeof(struct inode));

    if (!tmpfs_root)
        return -ENOMEM;

    tmpfs_root->name = "tmp";
    tmpfs_root->id   = (vino_t) tmpfs_root;
    tmpfs_root->type = FS_DIR;
    tmpfs_root->size = 0;
    tmpfs_root->fs   = &tmpfs;
    tmpfs_root->p    = NULL;

    vfs_bind(dir, tmpfs_root);

    return 0;
}

struct fs tmpfs = {
    .name   = "tmpfs",
    .init   = tmpfs_init,
    .mount  = tmpfs_mount,

    .iops = {
        .read    = tmpfs_read,
        .write   = tmpfs_write,
        .ioctl   = __vfs_nosys,
        .readdir = virtfs_readdir,
        .close   = tmpfs_close,

        .vmknod  = virtfs_vmknod,
        .vfind   = virtfs_vfind,
        .vget    = tmpfs_vget,
    },
    
    .fops = {
        .open    = tmpfs_file_open,
        .read    = posix_file_read,
        .write   = posix_file_write, 
        .ioctl   = posix_file_ioctl,
        .lseek   = posix_file_lseek,
        .readdir = posix_file_readdir,

        .can_read  = tmpfs_file_can_read,
        .can_write = tmpfs_file_can_write,
        .eof       = tmpfs_file_eof,
    },
};
