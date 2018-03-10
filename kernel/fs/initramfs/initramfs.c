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

#include <boot/boot.h>

struct inode *ramdisk_dev_node = NULL;

void load_ramdisk(module_t *rd_module)
{
    /* Ramdisk is the first module */
    void *ramdisk = (void *) rd_module->addr;
    size_t ramdisk_size = rd_module->size;

    printk("[0] Kernel: Ramdisk %p [%d]\n", ramdisk, ramdisk_size);

    ramdev_private_t *p = kmalloc(sizeof(ramdev_private_t));
    *p = (ramdev_private_t){.addr = ramdisk};

    ramdisk_dev_node = kmalloc(sizeof(struct inode));

    *ramdisk_dev_node = (struct inode) {
        .name = "ramdisk",
        .type = FS_DIR,
        .fs   = &devfs,
        .dev  = &ramdev,
        .size = ramdisk_size,
        .p    = p,
    };

    struct inode *root = NULL;
    int ret = initramfs.load(ramdisk_dev_node, &root);
    
    if (ret) {
        printk("Error code = %d\n", ret);
        panic("Could not load ramdisk\n");
    }

    vfs.mount_root(root);
}

static int cpio_roofs_inode(struct inode *sp, struct inode **inode)
{
    struct inode *node = kmalloc(sizeof(struct inode));

    if (!node)
        return -ENOMEM;

    node->id   = (vino_t) node;
    node->name = NULL;
    node->size = 0;
    node->type = FS_DIR;
    node->fs   = &initramfs;
    node->dev  = NULL;
    node->p    = kmalloc(sizeof(cpiofs_private_t));

    node->uid  = 1;
    node->gid  = 1;
    node->mask = 0555;  /* r-xr-xr-x */

    cpiofs_private_t *p = node->p;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = 0;
    p->next   = NULL;

    if (inode)
        *inode = node;

    return 0;
}

static int cpio_new_inode(char *name, cpio_hdr_t *hdr, size_t sz, size_t data, struct inode *sp, struct inode **inode)
{
    struct inode *node = kmalloc(sizeof(struct inode));

    if (!node)
        return -ENOMEM;

    uint32_t type = 0;
    switch (hdr->mode & CPIO_TYPE_MASK) {
        case CPIO_TYPE_RGL:    type = FS_RGL; break;
        case CPIO_TYPE_DIR:    type = FS_DIR; break;
        case CPIO_TYPE_SLINK:  type = FS_SYMLINK; break;
        case CPIO_TYPE_BLKDEV: type = FS_BLKDEV; break;
        case CPIO_TYPE_CHRDEV: type = FS_CHRDEV; break;
        case CPIO_TYPE_FIFO:   type = FS_FIFO; break;
        case CPIO_TYPE_SOCKET: type = FS_SOCKET; break;
    }
    
    node->id   = (vino_t) node;
    node->name = name;
    node->size = sz;
    node->type = type;
    node->fs   = &initramfs;
    node->dev  = NULL;
    node->p    = kmalloc(sizeof(cpiofs_private_t));

    node->uid  = 1;
    node->gid  = 1;
    node->mask = hdr->mode & CPIO_PERM_MASK;

    cpiofs_private_t *p = node->p;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = data;
    p->next   = NULL;

    if (inode)
        *inode = node;

    return 0;
}

static struct inode *cpiofs_new_child_node(struct inode *parent, struct inode *child)
{
    if (!parent || !child)  /* Invalid inode */
        return NULL;

    if (parent->type != FS_DIR) /* Adding child to non directory parent */
        return NULL;

    struct inode *tmp = ((cpiofs_private_t*)parent->p)->dir;
    ((cpiofs_private_t *) child->p)->next = tmp;
    ((cpiofs_private_t *) parent->p)->dir = child;

    ((cpiofs_private_t *) parent->p)->count++;
    ((cpiofs_private_t *) child->p)->parent = parent;

    return child;
}

static struct inode *cpiofs_find(struct inode *root, const char *path)
{
    //printk("cpiofs_find(root=%p, path=%s)\n", root, path);
    char **tokens = tokenize(path, '/');

    if (root->type != FS_DIR)   /* Not even a directory */
        return NULL;

    struct inode *cur = root;
    struct inode *dir = ((cpiofs_private_t *) cur->p)->dir;

    if (!dir) { /* Directory has no children */
        if (*tokens == NULL)
            return root;
        else
            return NULL;
    }

    int flag;
    foreach (token, tokens) {
        flag = 0;
        forlinked (e, dir, ((cpiofs_private_t *) e->p)->next) {
            if (e->name && !strcmp(e->name, token)) {
                cur = e;
                dir = ((cpiofs_private_t *) e->p)->dir;
                flag = 1;
                goto next;
            }
        }

        next:

        if (!flag)  /* No such file or directory */
            return NULL;

        continue;
    }

    free_tokens(tokens);

    return cur;
}

static int cpiofs_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    printk("cpiofs_vfind(parent=%p, name=%s, child=%p)\n", parent, name, child);

    struct inode *root = (struct inode *) parent->id;

    if (root->type != FS_DIR)   /* Not even a directory */
        return -ENOTDIR;

    struct inode *dir = ((cpiofs_private_t *) root->p)->dir;

    if (!dir)   /* Directory has no children */
        return -ENOENT;

    forlinked (e, dir, ((cpiofs_private_t *) e->p)->next) {
        if (e->name && !strcmp(e->name, name)) {
            if (child) {
                child->id   = (vino_t) e;
                child->type = e->type;
                child->uid  = e->uid;
                child->gid  = e->gid;
                child->mask = e->mask;
            }

            return 0;
        }
    }

    return -ENOENT;
}

static int cpiofs_vget(struct vnode *vnode, struct inode **inode)
{
    printk("cpiofs_vget(vnode=%p, inode=%p)\n", vnode, inode);
    *inode = (struct inode *) vnode->id;
    return 0;
}

static int cpiofs_load(struct inode *dev, struct inode **super)
{
    /* Allocate the root node */
    struct inode *rootfs = NULL;
    cpio_roofs_inode(dev, &rootfs);

    cpio_hdr_t hdr;
    size_t offset = 0;
    size_t size = 0;

    for (; offset < dev->size; 
          offset += sizeof(cpio_hdr_t) + (hdr.namesize+1)/2*2 + (size+1)/2*2) {

        size_t data_offset = offset;
        vfs.read(dev, data_offset, sizeof(cpio_hdr_t), &hdr);

        if (hdr.magic != CPIO_BIN_MAGIC) { /* Invalid CPIO archive */
            printk("Invalid CPIO archive\n");
            return -1;
        }

        size = hdr.filesize[0] * 0x10000 + hdr.filesize[1];
        
        data_offset += sizeof(cpio_hdr_t);
        
        char path[hdr.namesize];
        vfs.read(dev, data_offset, hdr.namesize, path);

        if (!strcmp(path, ".")) continue;
        if (!strcmp(path, "TRAILER!!!")) break; /* End of archive */

        char *dir  = NULL;
        char *name = NULL;

        for (int i = hdr.namesize - 1; i >= 0; --i) {
            if (path[i] == '/') {
                path[i] = '\0';
                name = &path[i+1];
                dir  = path;
                break;
            }
        }

        if (!name) {
            name = path;
            dir  = "/";
        }
        
        data_offset += hdr.namesize + (hdr.namesize % 2);

        struct inode *_node = NULL; 
        cpio_new_inode(strdup(name), &hdr, size, data_offset, dev, &_node);

        struct inode *parent = cpiofs_find(rootfs, dir);
        cpiofs_new_child_node(parent, _node);
    }

    if (super)
        *super = rootfs;

    return 0;
}

static ssize_t cpiofs_read(struct inode *node, off_t offset, size_t len, void *buf_p)
{
    //printk("cpiofs_read(node=%p, offset=%d, len=%d, buf_p=%p)\n", node, offset, len, buf_p);
    if (offset >= (off_t) node->size)
        return 0;

    len = MIN(len, node->size - offset);

    cpiofs_private_t *p = node->p;
    struct inode *super = p->super;
    return vfs.read(super, p->data + offset, len, buf_p);
}

static ssize_t cpiofs_readdir(struct inode *node, off_t offset, struct dirent *dirent)
{
    if ((size_t) offset == ((cpiofs_private_t *) node->p)->count)
        return 0;

    int i = 0;
    struct inode *dir = ((cpiofs_private_t *) node->p)->dir;

    forlinked (e, dir, ((cpiofs_private_t *) e->p)->next) {
        if (i == offset) {
            strcpy(dirent->d_name, e->name);   // FIXME
            break;
        }
        ++i;
    }

    return i == offset;
}

static int cpiofs_eof(struct file *file)
{
    if (file->node->type == FS_DIR) {
        return (size_t) file->offset >= ((cpiofs_private_t *) file->node->p)->count;
    } else {
        return (size_t) file->offset >= file->node->size;
    }
}

struct fs initramfs = {
    .name = "initramfs",
    .load = &cpiofs_load,

    .iops = {
        .read    = &cpiofs_read,
        .readdir = &cpiofs_readdir,
        .vfind   = cpiofs_vfind,
        .vget    = cpiofs_vget,
    },
    
    .fops = {
        .open    = generic_file_open,
        .read    = posix_file_read,
        .readdir = posix_file_readdir,
        .lseek   = posix_file_lseek,

        .eof = cpiofs_eof,
    },
};
