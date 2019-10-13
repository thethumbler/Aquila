#include <ds/bitmap.h>
#include <core/panic.h>
#include <minix.h>

static inline off_t minix_inode_off(struct minix *desc, ino_t ino)
{
    /* invalid inode */
    if (!ino || ino >= desc->superblock->ninodes)
        return -EINVAL;

    return desc->inode_table + (ino - 1) * desc->inode_size;
}

int minix_inode_read(struct minix *desc, ino_t ino, struct minix_inode *ref)
{
    off_t off;

    if ((off = minix_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
        size_t size = sizeof(struct minix_inode);
        ssize_t r;

        if ((r = vfs_read(desc->supernode, off, size, ref)) != (ssize_t) size) {
            if (r < 0)
                return r;

            /* FIXME Check for further errors? */
            return -EINVAL;
        }
    }

    return 0;
}

int minix_inode_write(struct minix *desc, ino_t ino, struct minix_inode *ref)
{
    off_t off;

    if ((off = minix_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
        size_t size = sizeof(struct minix_inode);
        ssize_t r;

        if ((r = vfs_write(desc->supernode, off, size, ref)) != (ssize_t) size) {
            if (r < 0)
                return r;

            /* FIXME Check for further errors? */
            return -EINVAL;
        }
    }

    return 0;
}

ssize_t minix_inode_block_read(struct minix *desc, struct minix_inode *m_inode, size_t idx, void *buf)
{
    size_t bs = desc->bs;
    size_t nr_blocks = (m_inode->size + bs - 1) / bs;
    size_t p = bs / sizeof(uint16_t);

    int err = -EINVAL;

    if (idx >= nr_blocks) {
        return -EINVAL;
    }

    if (idx < MINIX_DIRECT_ZONES) {
        return minix_block_read(desc, m_inode->zones[idx], buf);
    }

    idx -= MINIX_DIRECT_ZONES;

    if (idx < p) {
        uint16_t *iblock = NULL;
        if ((err = minix_bcache_get(desc, m_inode->zones[MINIX_DIRECT_ZONES], (void **) &iblock)))
            goto error;

        blk_t block = iblock[idx];
        return minix_block_read(desc, block, buf);
    }

    idx -= p;

    if (idx < p * p) {
        blk_t dizone = m_inode->zones[MINIX_DIRECT_ZONES+1];

        if (!dizone)
            panic("What0?\n");

        uint16_t *iblock = NULL;
        if ((err = minix_bcache_get(desc, m_inode->zones[MINIX_DIRECT_ZONES+1], (void **) &iblock)))
            goto error;

        blk_t block1 = iblock[idx / p];

        if (!block1)
            panic("What1?\n");

        if ((err = minix_bcache_get(desc, block1, (void **) &iblock)))
            goto error;

        blk_t block2 = iblock[idx % p];

        if (!block2)
            panic("What2?\n");

        return minix_block_read(desc, block2, buf);
    }

    return -EINVAL;

error:
    return err;
}

ssize_t minix_inode_block_write(struct minix *desc, struct minix_inode *inode, ino_t ino, size_t idx, void *buf)
{
    size_t p = desc->bs / 2;    /* pointers per block */

    if (idx < MINIX_DIRECT_ZONES) {
        if (!inode->zones[idx]) {    /* allocate */
            inode->zones[idx] = minix_block_alloc(desc);
            minix_inode_write(desc, ino, inode);
        }

        minix_block_write(desc, inode->zones[idx], buf);
    } else if (idx < MINIX_DIRECT_ZONES + p) {
        if (!inode->zones[MINIX_DIRECT_ZONES+1]) {    /* Allocate */
            inode->zones[MINIX_DIRECT_ZONES+1] = minix_block_alloc(desc);
            minix_inode_write(desc, ino, inode);
        }

        uint16_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        minix_block_read(desc, inode->zones[MINIX_DIRECT_ZONES+1], tmp);

        uint16_t block = tmp[idx - MINIX_DIRECT_ZONES];

        if (!block) {   /* Allocate */
            tmp[idx - MINIX_DIRECT_ZONES] = minix_block_alloc(desc);
            minix_block_write(desc, inode->zones[MINIX_DIRECT_ZONES+1], tmp);
        }

        kfree(tmp);
        minix_block_write(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

ino_t minix_inode_alloc(struct minix *desc)
{
    /* look for a free inode */
    ino_t ino = 0;
    for (; ino < desc->imap.max_idx && bitmap_check(&desc->imap, ino); ++ino);

    /* out of inodes */
    if (ino == desc->imap.max_idx)
        return 0;

    /* update bitmap */
    bitmap_set(&desc->imap, ino);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);

    return ino;
}

void minix_inode_free(struct minix *desc, ino_t ino)
{
    if (ino >= desc->imap.max_idx)
        /* TODO warn */
        return;

    /* update bitmap */
    bitmap_clear(&desc->imap, ino);

    /* sync bitmap -- TODO defer */
    vfs_write(desc->supernode, desc->imap_off, desc->superblock->imap_blocks * desc->bs, desc->imap.map);
}
