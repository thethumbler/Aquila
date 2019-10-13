#ifndef _EXT2_H
#define _EXT2_H

#include <core/system.h>
#include <fs/vfs.h>

#define EXT2_SIGNATURE  0xEF53

struct ext2_optional_features_flags {
    uint8_t preallocate_for_directories : 1;
    uint8_t afs_server_inodes : 1;
    uint8_t has_journaling : 1;     // for EXT 3
    uint8_t inodes_have_extended_attributes : 1;
    uint8_t can_be_resized : 1;
    uint8_t directories_use_hash_index : 1;
} __packed;

struct ext2_required_features_flags {
    uint8_t compressed : 1;
    uint8_t directories_has_types : 1;
    uint8_t needs_to_replay_journal : 1;
    uint8_t uses_journal_device : 1;
} __packed;

struct ext2_read_only_features_flags {
    uint8_t sparse_sb_and_gdt : 1;
    uint8_t uses_64bit_file_size : 1;
    uint8_t directories_contents_are_binary_tree : 1;
} __packed;

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

    uint16_t    times_mounted;  // after last fsck
    uint16_t    allowed_times_mounted;
    uint16_t    ext2_signature;
    uint16_t    filesystem_state;
    uint16_t    on_error;
    uint16_t    minor_version;
    uint32_t    last_fsck;
    uint32_t    time_between_fsck;  // Time allowed between fsck(s)
    uint32_t    os_id;
    uint32_t    major_version;
    uint16_t    uid;
    uint16_t    gid;

    /* for extended superblock fields ... which is always assumed */
    uint32_t    first_non_reserved_inode;
    uint16_t    inode_size;
    uint16_t    sb_block_group;

    struct ext2_optional_features_flags   optional_features;
    struct ext2_required_features_flags   required_features;
    struct ext2_read_only_features_flags  read_only_features;

    uint8_t     fsid[16];
    uint8_t     name[16];
    uint8_t     path_last_mounted_to[64];
    uint32_t    compression_algorithms;
    uint8_t     blocks_to_preallocate_for_files;
    uint8_t     blocks_to_preallocate_for_directories;
    uint16_t    __unused__;
    uint8_t     jid[16];    // Journal ID
    uint32_t    jinode;     // Journal inode
    uint32_t    jdevice;    // Journal device
    uint32_t    head_of_orphan_inode_list;  // What the hell is this !?
} __packed;

/*
 * \ingroup fs-ext2
 * \brief block group descriptor
 */
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

/**
 * \ingroup fs-ext2
 * \brief ext2 on disk inode
 */
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

/**
 * \ingroup fs-ext2
 * \brief ext2 directory entry
 */
struct ext2_dentry {
    /** inode number */
    uint32_t    ino;
    /** entry total size */
    uint16_t    size;
    /** name length */
    uint8_t     length;
    /** entry type */
    uint8_t     type;
    /** name will be here */
    uint8_t     name[0];
} __packed;

#define EXT2_DENTRY_TYPE_BAD   0
#define EXT2_DENTRY_TYPE_RGL   1
#define EXT2_DENTRY_TYPE_DIR   2
#define EXT2_DENTRY_TYPE_CHR   3
#define EXT2_DENTRY_TYPE_BLK   4
#define EXT2_DENTRY_TYPE_FIFO  5
#define EXT2_DENTRY_TYPE_SCKT  6
#define EXT2_DENTRY_TYPE_SLINK 7

#define EXT2_DIRECT_POINTERS 12

/**
 * \ingroup fs-ext2
 * \brief ext2 filesystem
 */
struct ext2 {
    struct vnode *supernode;
    struct ext2_superblock *superblock;
    struct ext2_group *groups;

    /** number of block group descriptors */
    size_t ngroups;

    size_t bs;
    struct vcache *vcache;
};

extern struct fs ext2fs;

/* super.c */
void ext2_superblock_rewrite(struct ext2 *desc);

/* block.c */
void ext2_bgd_table_rewrite(struct ext2 *desc);
void *ext2_block_read(struct ext2 *desc, uint32_t number, void *buf);
void *ext2_block_write(struct ext2 *desc, uint32_t number, void *buf);
uint32_t ext2_block_alloc(struct ext2 *desc);
void ext2_block_free(struct ext2 *desc, uint32_t block);

/* inode.c */
int ext2_inode_read(struct ext2 *desc, ino_t inode, struct ext2_inode *ref);
int ext2_inode_write(struct ext2 *desc, ino_t inode, struct ext2_inode *i);

size_t ext2_inode_block_read(struct ext2 *desc, struct ext2_inode *inode, size_t idx, void *buf);
size_t ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf);
ino_t ext2_inode_alloc(struct ext2 *desc);

/* dentry.c */
uint32_t ext2_dentry_find(struct ext2 *desc, struct ext2_inode *inode, const char *name);
int ext2_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode);

/* iops.c */
ssize_t ext2_read(struct vnode *node, off_t offset, size_t size, void *buf);
ssize_t ext2_write(struct vnode *node, off_t offset, size_t size, void *buf);
ssize_t ext2_readdir(struct vnode *dir, off_t offset, struct dirent *dirent);
int ext2_finddir(struct vnode *dir, const char *fn, struct dirent *dirent);

int ext2_trunc(struct vnode *inode, off_t len);
int ext2_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref);

int ext2_vget(struct vnode *super, ino_t ino, struct vnode **ref);

int ext2_inode_build(struct ext2 *desc, ino_t ino, struct vnode **ref_inode);  /* XXX */
int ext2_inode_sync(struct vnode *inode);

#endif 
