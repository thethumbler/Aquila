/**
 * \defgroup fs-minix kernel/fs/minix
 * \brief minix filesystem
 */
#include <core/system.h>
#include <core/module.h>

#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/vcache.h>
#include <fs/bcache.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

#include <minix.h>

MALLOC_DEFINE(M_MINIX, "minix", "minix filesystem structure");
MALLOC_DEFINE(M_MINIX_SB, "minix-sb", "minix filesystem superblock structure");

int minix_inode_build(struct minix *desc, ino_t ino, struct vnode **ref)
{
    int err = 0;
    struct vnode *inode = NULL;

    /* In open inode table? */
    if ((inode = vcache_find(desc->vcache, ino)))
        goto found;

    inode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!inode) return -ENOMEM;

    struct minix_inode minix_inode;
    
    if ((err = minix_inode_read(desc, ino, &minix_inode)))
        goto error;

    inode->ino   = ino;
    inode->size  = minix_inode.size;
    inode->mode  = minix_inode.mode;
    inode->uid   = minix_inode.uid;
    inode->gid   = minix_inode.gid;
    inode->nlink = minix_inode.nlinks;

    //node->atim.tv_sec  = i.last_access_time;
    //node->atim.tv_nsec = 0;
    inode->mtime.tv_sec  = minix_inode.mtime;
    inode->mtime.tv_nsec = 0;
    //node->ctim.tv_sec  = i.creation_time;
    //node->ctim.tv_nsec = 0;

    inode->fs = &minixfs;
    inode->p  = desc;

    vcache_insert(desc->vcache, inode);

found:
    if (ref)
        *ref = inode;

    return 0;

error:
    if (inode)
        kfree(inode);

    return err;
}

int minix_inode_sync(struct vnode *inode)
{
    int err = 0;

    struct minix *desc = inode->p;
    struct minix_inode minix_inode;
    
    if ((err = minix_inode_read(desc, inode->ino, &minix_inode)))
        return err;

    minix_inode.size   = inode->size;
    minix_inode.mode   = inode->mode;
    minix_inode.uid    = inode->uid;
    minix_inode.gid    = inode->gid;
    minix_inode.nlinks = inode->nlink;
    minix_inode.mtime  = inode->mtime.tv_sec;

    return minix_inode_write(desc, inode->ino, &minix_inode);
}

/* ================== VFS routines ================== */

static int minix_init()
{
    return vfs_install(&minixfs);
}

static int minix_load(struct vnode *dev, struct vnode **super)
{
    int err = 0;

    struct minix_superblock *sb = NULL;
    struct minix *desc = NULL;
   
    sb = kmalloc(sizeof(struct minix_superblock), &M_MINIX_SB, 0);
    if (sb == NULL) return -ENOMEM;

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
    desc = kmalloc(sizeof(struct minix), &M_MINIX, 0);
    if (!desc) goto e_nomem;

    desc->supernode   = dev;
    desc->superblock  = sb;

    if (version == 1) {
        desc->bs          = 1024UL << sb->block_size;

        desc->imap_off    = 1024UL + desc->bs;
        desc->zmap_off    = 1024UL + (1 + sb->imap_blocks) * desc->bs;

        desc->inode_table = 1024UL + (1 + sb->imap_blocks + sb->zmap_blocks) * desc->bs;

        desc->inode_size  = 32;
        desc->name_len    = name_len;
        desc->dentry_size = name_len + 2;
    } else {
        kfree(desc);
        return -ENOTSUP;
    }

    /* load inodes bitmap */
    desc->imap.map = kmalloc(sb->imap_blocks * desc->bs, &M_BUFFER, 0);
    desc->imap.max_idx = sb->ninodes;
    vfs_read(desc->supernode, desc->imap_off, sb->imap_blocks * desc->bs, desc->imap.map);

    /* load blocks bitmap */
    desc->zmap.map = kmalloc(sb->zmap_blocks * desc->bs, &M_BUFFER, 0);
    desc->zmap.max_idx = sb->nzones;
    vfs_read(desc->supernode, desc->zmap_off, sb->zmap_blocks * desc->bs, desc->zmap.map);

    desc->vcache = kmalloc(sizeof(struct vcache), &M_VCACHE, 0);
    if (!desc->vcache) goto e_nomem;

    vcache_init(desc->vcache);

    desc->bcache = kmalloc(sizeof(struct bcache), &M_BCACHE, 0);
    if (!desc->bcache) goto e_nomem;
    bcache_init(desc->bcache);

    if (super)
        minix_inode_build(desc, 1, super);
    
    return 0;

e_nomem:
    err = -ENOMEM;
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

    struct vnode *dev = NULL;
    int err;

    struct uio uio = {0};   /* FIXME */

    if ((err = vfs_lookup(args->dev, &uio, &dev, NULL)))
        goto error;

    struct vnode *fs;

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

    .vops = {
        .read    = minix_read,
        .write   = minix_write,
        .readdir = minix_readdir,
        .finddir = minix_finddir,
        .trunc   = minix_trunc,
        
        .vmknod  = minix_vmknod,
        .vget    = minix_vget,
    },

    .fops = {
        .open    = posix_file_open,
        .read    = posix_file_read,
        .write   = posix_file_write,
        .readdir = posix_file_readdir,
        .lseek   = posix_file_lseek,
        .trunc   = posix_file_trunc,

        .eof     = posix_file_eof,
    }
};

MODULE_INIT(minix, minix_init, NULL)
