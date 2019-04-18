#include <ds/bitmap.h>
#include <core/panic.h>
#include <minix.h>

int minix_inode_read(struct minix *desc, uint32_t id, struct minix_inode *ref)
{
    if (!id || id > desc->superblock->ninodes)    /* Invalid inode */
        return -EINVAL;

    id -= 1;
    
    if (ref) {
        off_t off   = desc->inode_table + id * desc->inode_size;
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

int minix_inode_write(struct minix *desc, uint32_t id, struct minix_inode *ref)
{
    if (!id || id > desc->superblock->ninodes)    /* Invalid inode */
        return -EINVAL;

    id -= 1;

    if (ref) {
        off_t off   = desc->inode_table + id * desc->inode_size;
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
    } else if (idx < MINIX_DIRECT_ZONES + p) {
        uint16_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);

        if ((err = minix_block_read(desc, m_inode->zones[MINIX_DIRECT_ZONES+1], tmp)) < 0)
            goto error;

        uint32_t block = tmp[idx - MINIX_DIRECT_ZONES];
        kfree(tmp);

        return minix_block_read(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

error:
    return err;
}

ssize_t minix_inode_block_write(struct minix *desc, struct minix_inode *inode, uint32_t inode_id, size_t idx, void *buf)
{
    size_t p = desc->bs / 2;    /* Pointers per block */

    if (idx < MINIX_DIRECT_ZONES) {
        if (!inode->zones[idx]) {    /* Allocate */
            inode->zones[idx] = minix_block_allocate(desc);
            minix_inode_write(desc, inode_id, inode);
        }
        minix_block_write(desc, inode->zones[idx], buf);
    } else if (idx < MINIX_DIRECT_ZONES + p) {
        if (!inode->zones[MINIX_DIRECT_ZONES+1]) {    /* Allocate */
            inode->zones[MINIX_DIRECT_ZONES+1] = minix_block_allocate(desc);
            minix_inode_write(desc, inode_id, inode);
        }

        uint16_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        minix_block_read(desc, inode->zones[MINIX_DIRECT_ZONES+1], tmp);

        uint16_t block = tmp[idx - MINIX_DIRECT_ZONES];

        if (!block) {   /* Allocate */
            tmp[idx - MINIX_DIRECT_ZONES] = minix_block_allocate(desc);
            minix_block_write(desc, inode->zones[MINIX_DIRECT_ZONES+1], tmp);
        }

        kfree(tmp);
        minix_block_write(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

uint32_t minix_inode_allocate(struct minix *desc)
{
    printk("minix_inode_allocate(desc=%p)\n", desc);

    char buf[desc->bs];

    uint32_t inode = 0, idx = 0, block = 0;
    bitmap_t bm = {0};

    for (unsigned i = 0; i < desc->superblock->imap_blocks; ++i) {
        minix_block_read(desc, desc->imap / desc->bs + i, &buf);

        bm.map = (uint32_t *) &buf;
        bm.max_idx = desc->bs;
        
        /* Look for a free inode */
        for (idx = 0; idx < bm.max_idx && bitmap_check(&bm, idx); ++idx);

        if (idx == bm.max_idx)
            continue;

        block = i;
        inode = idx + i * bm.max_idx;
        break;
    }

    if (!inode)
        return 0;

    /* Update bitmap */
    bitmap_set(&bm, idx);
    minix_block_write(desc, desc->imap / desc->bs + block, &buf);

    return inode;
}
