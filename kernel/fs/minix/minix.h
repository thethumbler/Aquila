#ifndef _MINIX_H
#define _MINIX_H

#include <core/system.h>
#include <ds/bitmap.h>
#include <ds/hashmap.h>

#include <fs/vfs.h>
#include <fs/vcache.h>
#include <fs/bcache.h>

#define MINIX_MAGIC    0x137F   /* Minix v1, 14 char names */
#define MINIX_MAGIC2   0x138F   /* Minix v1, 30 char names */

#define MINIX2_MAGIC   0x2468   /* Minix v2, 14 char names */
#define MINIX2_MAGIC2  0x2478   /* Minix v2, 30 char names */

#define MINIX3_MAGIC   0x4D5A   /* Minix v3, 60 char names */

#define MINIX_DIRECT_ZONES  7

/**
 * \ingroup fs-minix
 * \brief minix v1/v2 superblock
 */
struct minix_superblock {
    uint16_t    ninodes;
    uint16_t    nzones;
    uint16_t    imap_blocks;
    uint16_t    zmap_blocks;

    uint16_t    firstdatazone;
    uint16_t    block_size;

    uint32_t    max_size;
    uint16_t    magic;
    uint16_t    state;
};

/**
 * \ingroup fs-minix
 * \brief minix v3 superblock
 */
struct minix3_superblock {
	uint32_t    ninodes;
	uint16_t    pad0;
	uint16_t    imap_blocks;
	uint16_t    zmap_blocks;
	uint16_t    firstdatazone;
	uint16_t    log_zone_size;
	uint16_t    pad1;
	uint32_t    max_size;
	uint32_t    zones;
	uint16_t    magic;
	uint16_t    pad2;
	uint16_t    blocksize;
	uint8_t     disk_version;
};

/**
 * \ingroup fs-minix
 * \brief minix on-disk inode
 */
struct minix_inode {
    uint16_t    mode;
    uint16_t    uid;
    uint32_t    size;
    uint32_t    mtime;
    uint8_t     gid;
    uint8_t     nlinks;
    uint16_t    zones[9];
};

/**
 * \ingroup fs-minix
 * \brief minix directory entry
 */
struct minix_dentry {
    uint16_t    ino;
    uint8_t     name[0];    // Name will be here
};

/**
 * \ingroup fs-minix
 * \brief minix filesystem
 */
struct minix {
    struct vnode *supernode;
    struct minix_superblock *superblock;

    size_t   inode_size;
    size_t   dentry_size;
    size_t   name_len;

    uint32_t imap_off;
    uint32_t zmap_off;

    struct bitmap imap;
    struct bitmap zmap;

    int imap_dirty;
    int zmap_dirty;

    uint32_t inode_table;
    uint32_t data_zone;
    size_t bs;

    struct vcache *vcache;
    struct bcache *bcache;
};

extern struct fs minixfs;

/* super.c */
void minix_superblock_rewrite(struct minix *desc);

typedef uint32_t blk_t;

/* block.c */
blk_t minix_block_alloc(struct minix *desc);
void  minix_block_free(struct minix *desc, blk_t blk);
ssize_t minix_block_read(struct minix *desc, blk_t blk, void *buf);
ssize_t minix_block_write(struct minix *desc, blk_t blk, void *buf);
int minix_bcache_get(struct minix *desc, blk_t blk, void **data);

/* inode.c */
ino_t minix_inode_alloc(struct minix *desc);
void minix_inode_free(struct minix *desc, ino_t ino);
int minix_inode_read(struct minix *desc, uint32_t id, struct minix_inode *ref);
int minix_inode_write(struct minix *desc, uint32_t id, struct minix_inode *ref);
ssize_t minix_inode_block_read(struct minix *desc, struct minix_inode *m_inode, size_t idx, void *buf);
ssize_t minix_inode_block_write(struct minix *desc, struct minix_inode *inode, uint32_t id, size_t idx, void *buf);

/* dentry.c */
uint32_t minix_dentry_find(struct minix *desc, struct minix_inode *m_inode, const char *name);
int minix_dentry_create(struct vnode *dir, const char *name, uint32_t inode, mode_t mode);

/* iops.c */
ssize_t minix_read(struct vnode *inode, off_t offset, size_t size, void *buf);
ssize_t minix_write(struct vnode *inode, off_t offset, size_t size, void *buf);

ssize_t minix_readdir(struct vnode *dir, off_t offset, struct dirent *dirent);
int minix_finddir(struct vnode *dir, const char *fn, struct dirent *dirent);

int minix_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int minix_vget(struct vnode *super, ino_t ino, struct vnode **ref);
int minix_trunc(struct vnode *inode, off_t len);

int minix_inode_build(struct minix *desc, ino_t ino, struct vnode **ref);
int minix_inode_sync(struct vnode *inode);

#endif  /* ! _MINIX_H */
