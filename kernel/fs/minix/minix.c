#include <core/system.h>
#include <core/module.h>

#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/icache.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

#include <minix.h>

int minix_inode_build(struct minix *desc, ino_t ino, struct inode **ref)
{
    int err = 0;
    struct inode *inode = NULL;

    /* In open inode table? */
    if ((inode = icache_find(desc->icache, ino)))
        goto found;

    inode = kmalloc(sizeof(struct inode));

    if (!inode)
        return -ENOMEM;

    memset(inode, 0, sizeof(struct inode));

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, ino, &m_inode)))
        goto error;

    inode->ino   = ino;
    inode->size  = m_inode.size;
    inode->mode  = m_inode.mode;
    inode->uid   = m_inode.uid;
    inode->gid   = m_inode.gid;
    inode->nlink = m_inode.nlinks;

    //node->atim.tv_sec  = i.last_access_time;
    //node->atim.tv_nsec = 0;
    inode->mtime.tv_sec  = m_inode.mtime;
    inode->mtime.tv_nsec = 0;
    //node->ctim.tv_sec  = i.creation_time;
    //node->ctim.tv_nsec = 0;

    inode->fs = &minixfs;
    inode->p  = desc;

    icache_insert(desc->icache, inode);

found:
    if (ref)
        *ref = inode;

    return 0;

error:
    if (inode)
        kfree(inode);

    return err;
}

/* ================== VFS routines ================== */

static int minix_init()
{
    vfs_install(&minixfs);
    return 0;
}

static int minix_load(struct inode *dev, struct inode **super)
{
    int err = 0;

    struct minix_superblock *sb = NULL;
    struct minix *desc = NULL;
   
    if (!(sb = kmalloc(sizeof(struct minix_superblock))))
        return -ENOMEM;

    if ((err = vfs_read(dev, 1024, sizeof(*sb), sb)) < 0)
        goto error;

    int version = 0;
    size_t name_len = 0;

    switch (sb->magic) {
        case MINIX_MAGIC:
            version  = 1;
            name_len = 14;
            break;
        case MINIX_MAGIC2:
            version  = 1;
            name_len = 30;
            break;
    }

    if (!version) {
        err = -EINVAL;
        goto error;
    }

    /* Build descriptor structure */
    if (!(desc = kmalloc(sizeof(struct minix)))) {
        err = -ENOMEM;
        goto error;
    }

    desc->supernode   = dev;
    desc->superblock  = sb;

    if (version == 1) {
        desc->bs          = 1024UL << sb->block_size;
        desc->imap        = 1024UL + desc->bs;
        desc->zmap        = 1024UL + (1 + sb->imap_blocks) * desc->bs;
        desc->inode_table = 1024UL + (1 + sb->imap_blocks + sb->zmap_blocks) * desc->bs;
        desc->inode_size  = 32;
        desc->name_len    = name_len;
        desc->dentry_size = name_len + 2;
    } else {
        kfree(desc);
        return -ENOTSUP;
    }

    desc->icache = kmalloc(sizeof(struct icache));
    
    if (!desc->icache)
        return -ENOMEM;

    icache_init(desc->icache);

    if (super)
        minix_inode_build(desc, 1, super);
    
    return 0;

error:
    if (sb)
        kfree(sb);

    return err;
}

static int minix_mount(const char *dir, int flags, void *_args)
{
    struct {
        char *dev;
        char *opt;
    } *args = _args;

    struct vnode vnode;
    struct inode *dev;
    int err;

    struct uio uio = {0};   /* FIXME */

    if ((err = vfs_lookup(args->dev, &uio, &vnode, NULL)))
        goto error;

    if ((err = vfs_vget(&vnode, &dev)))
        goto error;

    struct inode *fs;

    if ((err = minixfs.load(dev, &fs)))
        goto error;

    return vfs_bind(dir, fs);

error:
    return err;
}

struct fs minixfs = {
    .name  = "minix",
    .init  = minix_init,
    .load  = minix_load,
    .mount = minix_mount,

    .iops = {
        .read    = minix_read,
        .write   = minix_write,
        .readdir = minix_readdir,
        
        .vmknod  = minix_vmknod,
        .vfind   = minix_vfind,
        .vget    = minix_vget,
    },

    .fops = {
        .open    = posix_file_open,
        .read    = posix_file_read,
        .write   = posix_file_write,
        .readdir = posix_file_readdir,
        .lseek   = posix_file_lseek,

        .eof     = posix_file_eof,
    }
};

MODULE_INIT(minix, minix_init, NULL)
