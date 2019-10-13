/**
 * \defgroup fs-ext2 kernel/fs/ext2
 * \brief ext2 filesystem
 */
#include <core/panic.h>
#include <core/module.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/vcache.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

#include <ext2.h>

MALLOC_DEFINE(M_EXT2, "ext2", "ext2 filesystem structure");
MALLOC_DEFINE(M_EXT2_SB, "ext2-sb", "ext2 filesystem superblock structure");
MALLOC_DEFINE(M_EXT2_GROUP, "ext2-group", "ext2 block group descriptor");

int ext2_inode_build(struct ext2 *desc, ino_t ino, struct vnode **ref_inode)
{
    int err = 0;
    struct vnode *inode = NULL;

    /* In open inode table? */
    if ((inode = vcache_find(desc->vcache, ino)))
        goto found;

    printk("ext2_inode_build(desc=%p, ino=%d, ref_inode=%p)\n", desc, ino, ref_inode);
    
    inode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!inode) {
        /* TODO */
    }

    struct ext2_inode einode;
    
    if ((err = ext2_inode_read(desc, ino, &einode))) {

    }

    inode->ino   = ino;
    inode->size  = einode.size;
    inode->mode  = einode.mode;
    inode->uid   = einode.uid;
    inode->gid   = einode.gid;
    inode->nlink = einode.nlink;

    inode->atime.tv_sec  = einode.atime;
    inode->atime.tv_nsec = 0;
    inode->mtime.tv_sec  = einode.mtime;
    inode->mtime.tv_nsec = 0;
    inode->ctime.tv_sec  = einode.ctime;
    inode->ctime.tv_nsec = 0;

    inode->fs = &ext2fs;
    inode->p = desc;

    vcache_insert(desc->vcache, inode);

found:
    if (ref_inode)
        *ref_inode = inode;

    return 0;
}

int ext2_inode_sync(struct vnode *inode)
{
    printk("ext2_inode_sync(inode=%p)\n", inode);

    int err = 0;
    
    struct ext2 *desc = (struct ext2 *) inode->p;
    struct ext2_inode ext2_inode;
    memset(&ext2_inode, 0, sizeof(struct ext2_inode));

    size_t bs = desc->bs;

    ext2_inode.size   = inode->size;
    ext2_inode.blocks = (inode->size + bs)/bs;
    ext2_inode.mode   = inode->mode;
    ext2_inode.uid    = inode->uid;
    ext2_inode.gid    = inode->gid;
    ext2_inode.nlink  = inode->nlink;

    ext2_inode.atime = inode->atime.tv_sec;
    ext2_inode.mtime = inode->mtime.tv_sec;
    ext2_inode.ctime = inode->ctime.tv_sec;

    if ((err = ext2_inode_write(desc, inode->ino, &ext2_inode))) {

    }

    return 0;
}

/* ================== VFS routines ================== */

int ext2_init()
{
    return vfs_install(&ext2fs);
}

int ext2_load(struct vnode *dev, struct vnode **super)
{
    int err = 0;

    struct ext2_superblock *sb = NULL;
    struct ext2 *desc = NULL;

    sb = kmalloc(sizeof(struct ext2_superblock), &M_EXT2_SB, M_ZERO);
    if (!sb) goto e_nomem;

    /* read superblock */
    if ((err = vfs_read(dev, 1024, sizeof(struct ext2_superblock), sb)) < 0)
        goto error;

    /* valid ext2? */
    if (sb->ext2_signature != EXT2_SIGNATURE) {
        err = -EINVAL;
        goto error;
    }

    /* build descriptor structure */
    desc = kmalloc(sizeof(struct ext2), &M_EXT2, M_ZERO);
    if (!desc) goto e_nomem;

    desc->supernode = dev;
    desc->superblock = sb;
    desc->bs = 1024UL << sb->block_size;

    /* read block group descriptor table */
    uint32_t groups    = (desc->bs == 1024)? 2048 : desc->bs;
    size_t   ngroups   = (sb->nblocks + sb->gblocks - 1)/ sb->gblocks;
    size_t   groups_sz = ngroups * sizeof(struct ext2_group);

    desc->ngroups = ngroups;
    desc->groups  = kmalloc(groups_sz, &M_EXT2_GROUP, 0);
    if (!desc->groups) goto e_nomem;

    if ((err = vfs_read(desc->supernode, groups, groups_sz, desc->groups)) < 0)
        return err;

    desc->vcache = kmalloc(sizeof(struct vcache), &M_VCACHE, 0);
    
    if (!desc->vcache)
        return -ENOMEM;

    vcache_init(desc->vcache);

    if (super)
        ext2_inode_build(desc, 2, super);
    
    return 0;

e_nomem:
    err = -ENOMEM;
error:
    if (sb) kfree(sb);
    return err;
}

int ext2_mount(const char *dir, int flags, void *_args)
{
    printk("ext2_mount(dir=%s, flags=%x, args=%p)\n", dir, flags, _args);

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

    if ((err = ext2fs.load(dev, &fs)))
        goto error;

    return vfs_bind(dir, fs);

error:
    return err;
}

struct fs ext2fs = {
    .name  = "ext2",
    .init  = ext2_init,
    .load  = ext2_load,
    .mount = ext2_mount,

    .vops = {
        .read    = ext2_read,
        .write   = ext2_write,
        .readdir = ext2_readdir,
        .finddir = ext2_finddir,

        .trunc   = ext2_trunc,
        
        .vmknod  = ext2_vmknod,
        .vget    = ext2_vget,
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

MODULE_INIT(ext2, ext2_init, NULL)
