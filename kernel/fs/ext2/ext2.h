#ifndef _EXT2_H
#define _EXT2_H

/*
 * The second extended filesystem, ext2. The disk is cut into block
 * groups; each has a block bitmap, an inode bitmap and part of the inode
 * table, and the block group descriptor table (after the superblock)
 * says where they are. An inode has 12 direct blocks, then a singly, a
 * doubly and a triply indirect one. A directory's blocks hold entries of
 * different sizes, which make a list in each block.
 */

#include <core/system.h>
#include <fs/vfs.h>

#define EXT2_SIGNATURE  0xEF53

/* revision 1's features: compatible ones may be ignored; an incompatible
 * one that the code doesn't know means it can't use the filesystem, and a
 * read-only compatible one means it can't write to it */
#define EXT2_INCOMPAT_FILETYPE      0x0002  /* the entries' types */
#define EXT2_RO_COMPAT_SPARSE_SUPER 0x0001  /* backup superblocks in fewer groups */
#define EXT2_RO_COMPAT_LARGE_FILE   0x0002  /* sizes past 2 GiB (size_high) */

/* the features AquilaOS's ext2 handles: those mke2fs -t ext2 uses */
#define EXT2_INCOMPAT_SUPPORTED     EXT2_INCOMPAT_FILETYPE
#define EXT2_RO_COMPAT_SUPPORTED    (EXT2_RO_COMPAT_SPARSE_SUPER | EXT2_RO_COMPAT_LARGE_FILE)

/* revision 0's superblock has no inode size or first inode: they are
 * fixed */
#define EXT2_GOOD_OLD_INODE_SIZE    128

/** The superblock, as it is on the disk, 1024 bytes into it. */
struct ext2_superblock {
    /** number of inodes */
    uint32_t    ninodes;
    /** number of blocks */
    uint32_t    nblocks;
    /** number of reserved blocks */
    uint32_t    nrblocks;
    /** number of unallocated blocks */
    uint32_t    free_blocks;
    /** number of unallocated inodes */
    uint32_t    free_inodes;
    /** first data block */
    uint32_t    dblock;
    /** log(block size) */
    uint32_t    block_size;
    /** log(fragment size) */
    uint32_t    frag_size;
    /** number of blocks per block group */
    uint32_t    gblocks;
    /** number of fragments per block group */
    uint32_t    gfrags;
    /** number of inodes per block group */
    uint32_t    ginodes;
    /** last mount time */
    uint32_t    mtime;
    /** last write time */
    uint32_t    wtime;

    uint16_t    times_mounted;  /* since the last fsck */
    uint16_t    allowed_times_mounted;
    uint16_t    ext2_signature;
    uint16_t    filesystem_state;
    uint16_t    on_error;
    uint16_t    minor_version;
    uint32_t    last_fsck;
    uint32_t    time_between_fsck;  /* the time allowed between two fscks */
    uint32_t    os_id;
    uint32_t    major_version;
    uint16_t    uid;
    uint16_t    gid;

    /* revision 1 (major_version), not 0 */
    uint32_t    first_non_reserved_inode;
    uint16_t    inode_size;
    uint16_t    sb_block_group;

    uint32_t    feature_compat;
    uint32_t    feature_incompat;
    uint32_t    feature_ro_compat;

    uint8_t     fsid[16];
    uint8_t     name[16];
    uint8_t     path_last_mounted_to[64];
    uint32_t    compression_algorithms;
    uint8_t     blocks_to_preallocate_for_files;
    uint8_t     blocks_to_preallocate_for_directories;
    uint16_t    __unused__;
    uint8_t     jid[16];    /* the journal's ID (ext3) */
    uint32_t    jinode;     /* the journal's inode (ext3) */
    uint32_t    jdevice;    /* the journal's device (ext3) */
    uint32_t    head_of_orphan_inode_list;  /* inodes deleted while open (ext3) */
} __packed;

/** A block group descriptor, as it is on the disk. */
struct ext2_group {
    /** blocks bitmap */
    uint32_t    bmap;
    /** inodes bitmap */
    uint32_t    imap;
    /** inode table */
    uint32_t    inodes;
    /** number of unallocated blocks */
    uint16_t    free_blocks;
    /** number of unallocated inodes */
    uint16_t    free_inodes;
    uint16_t    directories_count;
    uint8_t     __unused__[14];
} __packed;

/** An inode, as it is on the disk. */
struct ext2_inode {
    uint16_t    mode;
    uint16_t    uid;
    uint32_t    size;
    uint32_t    atime;
    uint32_t    ctime;
    uint32_t    mtime;
    uint32_t    dtime;
    uint16_t    gid;
    uint16_t    nlink;
    uint32_t    blocks;
    uint32_t    flags;

    uint32_t    os_specific1;
    uint32_t    direct_pointer[12];
    uint32_t    singly_indirect_pointer;
    uint32_t    doubly_indirect_pointer;
    uint32_t    triply_indirect_pointer;

    uint32_t    generation_number;
    uint32_t    extended_attribute_block;
    uint32_t    size_high;
    uint32_t    fragment;
    uint8_t     os_specific2[12];
} __packed;

/** A directory entry, as it is on the disk. 0 as the inode number is an
 * unused entry; `size` is the distance to the next entry. */
struct ext2_dentry {
    /** inode number */
    uint32_t    ino;
    /** entry total size */
    uint16_t    size;
    /** name length */
    uint8_t     length;
    /** entry type */
    uint8_t     type;
    /** the name follows (not NUL-terminated) */
    uint8_t     name[0];
} __packed;

/* the entry types, with EXT2_INCOMPAT_FILETYPE */
#define EXT2_DENTRY_TYPE_BAD   0
#define EXT2_DENTRY_TYPE_RGL   1
#define EXT2_DENTRY_TYPE_DIR   2
#define EXT2_DENTRY_TYPE_CHR   3
#define EXT2_DENTRY_TYPE_BLK   4
#define EXT2_DENTRY_TYPE_FIFO  5
#define EXT2_DENTRY_TYPE_SCKT  6
#define EXT2_DENTRY_TYPE_SLINK 7

#define EXT2_DIRECT_POINTERS 12

/** A mounted ext2 filesystem. */
struct ext2 {
    struct vnode *supernode;    /* the device it's on */
    struct ext2_superblock *superblock;
    struct ext2_group *groups;  /* the block group descriptor table */

    /** number of block group descriptors */
    size_t ngroups;

    /** the block size */
    size_t bs;

    /** an inode's size on the disk (revision 0's, or the superblock's) */
    size_t inode_size;

    /** directory entries have their file's type (EXT2_INCOMPAT_FILETYPE);
     * without it, the byte is 0 */
    int filetype;

    /** its vnodes in use, by inode number */
    struct vcache *vcache;
};

extern struct fs ext2fs;

/* the functions that read or write the disk return 0, or a negative
 * errno: -EIO if not all of it was read or written */

/* super.c */
int ext2_superblock_rewrite(struct ext2 *desc);

/* block.c */
int ext2_io(struct ext2 *desc, int write, off_t off, size_t size, void *buf);
int ext2_bgd_table_rewrite(struct ext2 *desc);
int ext2_block_read(struct ext2 *desc, uint32_t number, void *buf);
int ext2_block_write(struct ext2 *desc, uint32_t number, void *buf);
int ext2_block_alloc(struct ext2 *desc, uint32_t *block);
int ext2_block_free(struct ext2 *desc, uint32_t block);

/* inode.c */
int ext2_inode_read(struct ext2 *desc, ino_t inode, struct ext2_inode *ref);
int ext2_inode_write(struct ext2 *desc, ino_t inode, struct ext2_inode *i);

int ext2_inode_block_read(struct ext2 *desc, struct ext2_inode *inode, size_t idx, void *buf);
int ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf);
int ext2_inode_alloc(struct ext2 *desc, ino_t *ino);

/* dentry.c */
int ext2_dentry_find(struct ext2 *desc, struct ext2_inode *inode, const char *name, ino_t *ino);
int ext2_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode);

/* vops.c and remove.c: the vnode operations */
ssize_t ext2_read(struct vnode *node, off_t offset, struct iobuf *io);
ssize_t ext2_write(struct vnode *node, off_t offset, struct iobuf *io);
ssize_t ext2_readdir(struct vnode *dir, off_t offset, struct dirent *dirent);
int ext2_finddir(struct vnode *dir, const char *fn, struct dirent *dirent);

int ext2_trunc(struct vnode *inode, off_t len);
int ext2_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref);

int ext2_vget(struct vnode *super, ino_t ino, struct vnode **ref);
int ext2_setattr(struct vnode *vnode);
int ext2_close(struct vnode *vnode);
int ext2_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int ext2_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio);
int ext2_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio);
void ext2_blocks_free(struct ext2 *desc, struct ext2_inode *inode, size_t from);
void ext2_inode_discard(struct ext2 *desc, ino_t ino);

/* ext2.c: an inode's vnode (the cached one, or a new one), and writing a
 * vnode back to its inode */
int ext2_inode_build(struct ext2 *desc, ino_t ino, struct vnode **ref_inode);
int ext2_inode_sync(struct vnode *inode);

#endif 
