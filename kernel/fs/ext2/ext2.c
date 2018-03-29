#include <core/panic.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <fs/itbl.h>
#include <bits/errno.h>
#include <ds/bitmap.h>

#include <ext2.h>

int ext2_inode_build(struct __ext2 *desc, size_t inode, struct inode **ref_inode)
{
    printk("ext2_inode_build(desc=%p, inode=%d, ref_inode=%p)\n", desc, inode, ref_inode);

    struct inode *node = NULL;

    /* In open inode table? */
    if ((node = itbl_find(desc->itbl, inode)))
        goto found;

    node = kmalloc(sizeof(struct inode));
    memset(node, 0, sizeof(*node));

    struct ext2_inode *i = ext2_inode_read(desc, inode);

    switch (i->type) {
        case EXT2_INODE_TYPE_FIFO:  node->type = FS_FIFO; break;
        case EXT2_INODE_TYPE_CHR:   node->type = FS_CHRDEV; break;
        case EXT2_INODE_TYPE_DIR:   node->type = FS_DIR; break;
        case EXT2_INODE_TYPE_BLK:   node->type = FS_BLKDEV; break;
        case EXT2_INODE_TYPE_RGL:   node->type = FS_RGL; break;
        case EXT2_INODE_TYPE_SLINK: node->type = FS_SYMLINK; break;
        case EXT2_INODE_TYPE_SCKT:  node->type = FS_SOCKET; break;
    }

    node->id   = inode;
    node->size = i->size;
    node->mask = i->permissions;
    node->uid  = i->uid;
    node->gid  = i->gid;

    node->fs   = &ext2fs;
    kfree(i);
    node->p = desc;

    itbl_insert(desc->itbl, node);

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
    /* Read Superblock */
    struct ext2_superblock *sb = kmalloc(sizeof(*sb));
    vfs_read(dev, 1024, sizeof(*sb), sb);

    /* Valid Ext2? */
    if (sb->ext2_signature != EXT2_SIGNATURE) {
        kfree(sb);
        return -EINVAL;
    }

    /* Build descriptor structure */
    struct __ext2 *desc = kmalloc(sizeof(struct __ext2));
    desc->supernode = dev;
    desc->superblock = sb;
    desc->bs = 1024UL << sb->block_size;

    /* Read block group descriptor table */
    uint32_t bgd_table = (desc->bs == 1024)? 2048 : desc->bs;
    size_t bgds_count = sb->blocks_count/sb->blocks_per_block_group;
    size_t bgds_size  = bgds_count * sizeof(struct ext2_block_group_descriptor);
    desc->bgds_count = bgds_count;
    desc->bgd_table = kmalloc(bgds_size);
    vfs_read(desc->supernode, bgd_table, bgds_size, desc->bgd_table);

    desc->itbl = kmalloc(sizeof(struct itbl));
    itbl_init(desc->itbl);

    if (super)
        ext2_inode_build(desc, 2, super);
    
    return 0;
}

int ext2_mount(const char *dir, int flags, void *data)
{
    printk("ext2_mount(dir=%s, flags=%x, data=%p)\n", dir, flags, data);

    struct {
        char *dev;
        char *opt;
    } *sdata = data;

    //printk("data{dev=%s, opt=%s}\n", sdata->dev, sdata->opt);

    struct vnode vnode;
    struct inode *dev;
    int ret;

    struct uio uio = {0};   /* FIXME */
    if ((ret = vfs_lookup(sdata->dev, &uio, &vnode, NULL))) {
        panic("Could not load device");
    }

    vfs_vget(&vnode, &dev);

    struct inode *fs;
    ext2fs.load(dev, &fs);
    vfs_bind(dir, fs);

    return 0;
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
