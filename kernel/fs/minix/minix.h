#ifndef _MINIX_H
#define _MINIX_H

#include <core/system.h>
#include <fs/vfs.h>

#define MINIX_MAGIC    0x137F   /* Minix v1, 14 char names */
#define MINIX_MAGIC2   0x138F   /* Minix v1, 30 char names */

#define MINIX2_MAGIC   0x2468   /* Minix v2, 14 char names */
#define MINIX2_MAGIC2  0x2478   /* Minix v2, 30 char names */

#define MINIX3_MAGIC   0x4D5A   /* Minix v3, 60 char names */

#define MINIX_DIRECT_ZONES  7

/* v1/v2 super block */
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
} __packed;

/* v3 super block */
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

struct minix_inode {
    uint16_t    mode;
    uint16_t    uid;
    uint32_t    size;
    uint32_t    mtime;
    uint8_t     gid;
    uint8_t     nlinks;
    uint16_t    zones[9];
} __packed;

struct minix_dentry {
    uint16_t    inode;
    uint8_t     name[0];    // Name will be here
} __packed;

struct minix {
    struct inode *supernode;
    struct minix_superblock *superblock;

    size_t   inode_size;
    size_t   dentry_size;
    size_t   name_len;

    uint32_t imap;
    uint32_t zmap;
    uint32_t inode_table;
    uint32_t data_zone;
    size_t bs;

    struct icache *icache;
};

extern struct fs minixfs;

/* super.c */
void minix_superblock_rewrite(struct minix *desc);

/* block.c */
ssize_t minix_block_read(struct minix *desc, uint32_t number, void *buf);
ssize_t minix_block_write(struct minix *desc, uint32_t number, void *buf);
uint32_t minix_block_allocate(struct minix *desc);

/* inode.c */
int minix_inode_read(struct minix *desc, uint32_t id, struct minix_inode *ref);
int minix_inode_write(struct minix *desc, uint32_t id, struct minix_inode *ref);
ssize_t minix_inode_block_read(struct minix *desc, struct minix_inode *m_inode, size_t idx, void *buf);
ssize_t minix_inode_block_write(struct minix *desc, struct minix_inode *inode, uint32_t id, size_t idx, void *buf);
uint32_t minix_inode_allocate(struct minix *desc);

/* dentry.c */
uint32_t minix_dentry_find(struct minix *desc, struct minix_inode *m_inode, const char *name);
int minix_dentry_create(struct vnode *dir, const char *name, uint32_t inode, mode_t mode);

/* iops.c */
ssize_t minix_read(struct inode *inode, off_t offset, size_t size, void *buf);
ssize_t minix_write(struct inode *inode, off_t offset, size_t size, void *buf);

ssize_t minix_readdir(struct inode *dir, off_t offset, struct dirent *dirent);
int minix_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref);
int minix_vfind(struct vnode *dir, const char *fn, struct vnode *child);
int minix_vget(struct vnode *vnode, struct inode **ref);

int minix_inode_build(struct minix *desc, size_t id, struct inode **ref);

#endif  /* ! _MINIX_H */
