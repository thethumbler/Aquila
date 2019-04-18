#include <core/panic.h>
#include <core/module.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/icache.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

#include <ext2.h>

MALLOC_DEFINE(M_EXT2, "ext2", "ext2 filesystem structure");
MALLOC_DEFINE(M_EXT2_SB, "ext2-sb", "ext2 filesystem superblock structure");
MALLOC_DEFINE(M_EXT2_BGD, "ext2-bgd", "ext2 block group descriptor");

int ext2_inode_build(struct ext2 *desc, size_t inode, struct inode **ref_inode)
{
    //printk("ext2_inode_build(desc=%p, inode=%d, ref_inode=%p)\n", desc, inode, ref_inode);
    
    int err = 0;
    struct inode *node = NULL;

    /* In open inode table? */
    if ((node = icache_find(desc->icache, inode)))
        goto found;

    node = kmalloc(sizeof(struct inode), &M_INODE, 0);
    memset(node, 0, sizeof(*node));

    struct ext2_inode i;
    
    if ((err = ext2_inode_read(desc, inode, &i))) {

    }

    node->ino   = inode;
    node->size  = i.size;
    node->mode  = i.mode;
    node->uid   = i.uid;
    node->gid   = i.gid;
    node->nlink = i.nlinks;

    node->atime.tv_sec  = i.atime;
    node->atime.tv_nsec = 0;
    node->mtime.tv_sec  = i.mtime;
    node->mtime.tv_nsec = 0;
    node->ctime.tv_sec  = i.ctime;
    node->ctime.tv_nsec = 0;

    node->fs   = &ext2fs;
    node->p = desc;

    icache_insert(desc->icache, node);

found:
    if (ref_inode)
        *ref_inode = node;

    return 0;
}

/* ================== VFS routines ================== */

int ext2_init()
{
    vfs_install(&ext2fs);
    return 0;
}

int ext2_load(struct inode *dev, struct inode **super)
{
    int err = 0;

    /* Read Superblock */
    struct ext2_superblock *sb;
    sb = kmalloc(sizeof(struct ext2_superblock), &M_EXT2_SB, 0);

    if ((err = vfs_read(dev, 1024, sizeof(struct ext2_superblock), sb)) < 0)
        return err;

    /* Valid Ext2? */
    if (sb->ext2_signature != EXT2_SIGNATURE) {
        kfree(sb);
        return -EINVAL;
    }

    /* Build descriptor structure */
    struct ext2 *desc = kmalloc(sizeof(struct ext2), &M_EXT2, 0);
    desc->supernode = dev;
    desc->superblock = sb;
    desc->bs = 1024UL << sb->block_size;

    /* Read block group descriptor table */
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_count  = (sb->blocks_count + sb->blocks_per_block_group - 1)/ sb->blocks_per_block_group;
    size_t bgds_size   = bgds_count * sizeof(struct ext2_block_group_descriptor);
    desc->bgds_count   = bgds_count;

    desc->bgd_table = kmalloc(bgds_size, &M_EXT2_BGD, 0);

    if (!desc->bgd_table)
        return -ENOMEM;

    if ((err = vfs_read(desc->supernode, bgd_table, bgds_size, desc->bgd_table)) < 0)
        return err;

    desc->icache = kmalloc(sizeof(struct icache), &M_ICACHE, 0);
    
    if (!desc->icache)
        return -ENOMEM;

    icache_init(desc->icache);

    if (super)
        ext2_inode_build(desc, 2, super);
    
    return 0;
}

int ext2_mount(const char *dir, int flags, void *_args)
{
    printk("ext2_mount(dir=%s, flags=%x, args=%p)\n", dir, flags, _args);

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

    .iops = {
        .read    = ext2_read,
        .write   = ext2_write,
        .readdir = ext2_readdir,
        
        .vmknod  = ext2_vmknod,
        .vfind   = ext2_vfind,
        .vget    = ext2_vget,
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

MODULE_INIT(ext2, ext2_init, NULL)
