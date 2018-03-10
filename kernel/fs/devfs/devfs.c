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
#include <core/panic.h>

#include <mm/mm.h>

#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <bits/errno.h>
#include <bits/dirent.h>

/* devfs root directory (usually mounted on '/dev') */
struct inode *dev_root = NULL;
struct vnode vdev_root;

static int devfs_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    printk("devfs_vfind(dir={id=%p, type=%d}, fn=%s, child=%p)\n", dir->id, dir->type, fn, child);
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    struct inode *inode = (struct inode *) dir->id;
    struct devfs_dir *_dir = (struct devfs_dir *) inode->p;
    struct inode *next = NULL;

    if (!_dir)  /* Directory not initialized */
        return -ENOENT;

    forlinked (file, _dir, file->next) {
        if (!strcmp(file->node->name, fn)) {
            next = file->node;
            goto found;
        }
    }

    return -ENOENT;    /* File not found */

found:
    if (child) {
        /* child->super should not be touched */
        child->id   = (vino_t) next;
        child->type = next->type;
        child->mask = next->mask;
        child->uid  = next->uid;
        child->gid  = next->gid;
    }

    return 0;
}

static int devfs_vget(struct vnode *vnode, struct inode **inode)
{
    if (!vnode)
        return -EINVAL;

    /* Inode is always present in memory */
    struct inode *node = (struct inode *) vnode->id;

    if (inode)
        *inode = node;

    return 0;
}

#if 0
static struct inode *devfs_traverse(struct vfs_path *path)
{
    //printk("devfs_traverse(path=%p)\n", path);
    struct fs_node *dir = path->mountpoint;

    foreach (token, path->tokens) {
        dir = devfs_find(dir, token);
    }

    return dir;
}
#endif

static ssize_t devfs_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    /* is node connected to a device handler? */
    if (!node->dev) 
        return -EINVAL;

    /* Not supported */
    if (!node->dev->read)
        return -ENOSYS;

    return node->dev->read(node, offset, size, buf);
}

static ssize_t devfs_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    if (!node->dev) /* is node connected to a device handler? */
        return -EINVAL;

    /* Not supported */
    if (!node->dev->write)
        return -ENOSYS;

    return node->dev->write(node, offset, size, buf);
}

static int devfs_create(struct vnode *vdir, const char *fn, int uid, int gid, int mode, struct inode **ref_node)
{
    int ret = 0;
    struct inode *node = kmalloc(sizeof(struct inode));

    if (!node) {
        ret = -ENOMEM;
        goto error;
    }

    memset(node, 0, sizeof(struct inode));
    
    node->name = strdup(fn);

    if (!node->name) {
        ret = -ENOMEM;
        goto error;
    }

    node->type = FS_RGL;
    node->fs   = &devfs;
    node->size = 0;
    node->uid  = uid;
    node->gid  = gid;
    node->mask = mode & 0x1FF;

    struct inode *dir = NULL;

    if (devfs.iops.vget(vdir, &dir)) {
        /* That's odd */
        ret = -EINVAL;
        goto error;
    }

    struct devfs_dir *_dir = (struct devfs_dir *) dir->p;
    struct devfs_dir *tmp  = kmalloc(sizeof(struct devfs_dir));

    if (!tmp) {
        ret = -ENOMEM;
        goto error;
    }

    tmp->next = _dir;
    tmp->node = node;

    dir->p = tmp;

    if (ref_node)
        *ref_node = node;

    return ret;

error:
    if (node) {
        if (node->name)
            kfree(node->name);
        kfree(node);
    }

    if (tmp)
        kfree(tmp);

    return ret;
}

static int devfs_mkdir(struct vnode *dir, const char *dname, int uid, int gid, int mode)
{
    int ret = 0;
    struct inode *inode = NULL;
    ret = devfs_create(dir, dname, uid, gid, mode, &inode);

    if (ret)
        return ret;

    inode->type = FS_DIR;
    return 0;
}

static int devfs_ioctl(struct inode *file, int request, void *argp)
{
    if (!file || !file->dev)
        return -EINVAL;

    if (!file->dev->ioctl)
        return -ENOSYS;

    return file->dev->ioctl(file, request, argp);
}

static ssize_t devfs_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    //printk("devfs_readdir(dir=%p, offset=%d, dirent=%p)\n", dir, offset, dirent);
    int i = 0;
    struct devfs_dir *_dir = (struct devfs_dir *) dir->p;

    if (!_dir)
        return 0;

    int found = 0;

    forlinked (e, _dir, e->next) {
        if (i == offset) {
            found = 1;
            strcpy(dirent->d_name, e->node->name);   // FIXME
            break;
        }
        ++i;
    }

    return found;
}

/* ================ File Operations ================ */

static int devfs_file_open(struct file *file)
{
    printk("devfs_file_open(file=%p)\n", file);
    if (file->node->type == FS_DIR) {
        return 0;
    } else {
        if (!file->node->dev)
            return -ENXIO;
        
        return file->node->dev->fops.open(file);
    }
}

static ssize_t devfs_file_read(struct file *file, void *buf, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    if (!file->node->dev->fops.read)
        return -ENOSYS;

    return file->node->dev->fops.read(file, buf, size);
}

static ssize_t devfs_file_write(struct file *file, void *buf, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    if (!file->node->dev->fops.write)
        return -ENOSYS;
    
    return file->node->dev->fops.write(file, buf, size);
}

static int devfs_file_ioctl(struct file *file, int request, void *argp)
{
    if (!file->node->dev)
        return -ENXIO;

    if (!file->node->dev->fops.ioctl)
        return -ENOSYS;

    return file->node->dev->fops.ioctl(file, request, argp);
}

static off_t devfs_file_lseek(struct file *file, off_t offset, int whence)
{
    if (!file->node->dev)
        return -ENXIO;

    if (!file->node->dev->fops.lseek)
        return -ENOSYS;

    return file->node->dev->fops.lseek(file, offset, whence);
}

static int devfs_file_can_read(struct file *file, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->fops.can_read(file, size);
}

static int devfs_file_can_write(struct file *file, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->fops.can_write(file, size);
}

static int devfs_file_eof(struct file *file)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->fops.eof(file);
}

static int devfs_init()
{
    //printk("[0] Kernel: devfs -> init()\n");
    dev_root = kmalloc(sizeof(struct inode));

    if (!dev_root)
        return -ENOMEM;

    dev_root->name = "dev";
    dev_root->id   = (vino_t) dev_root;
    dev_root->type = FS_DIR;
    dev_root->size = 0;
    dev_root->fs   = &devfs;
    dev_root->p    = NULL;

    vdev_root.super = dev_root;
    vdev_root.id    = (vino_t) dev_root;
    vdev_root.type  = FS_DIR;

    return 0;
}

struct fs devfs = {
    .name   = "devfs",
    .init   = devfs_init,

    .iops = {
        .create  = devfs_create,
        .mkdir   = devfs_mkdir,
        //.find    = devfs_find,
        .read    = devfs_read,
        .write   = devfs_write,
        .ioctl   = devfs_ioctl,
        .readdir = devfs_readdir,
        .vfind   = devfs_vfind,
        .vget    = devfs_vget,
    },
    
    .fops = {
        .open    = devfs_file_open,
        .read    = devfs_file_read,
        .write   = devfs_file_write, 
        .ioctl   = devfs_file_ioctl,
        .lseek   = devfs_file_lseek,
        .readdir = posix_file_readdir,

        .can_read  = devfs_file_can_read,
        .can_write = devfs_file_can_write,
        .eof       = devfs_file_eof,
    },
};
