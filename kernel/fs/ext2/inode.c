#include <ext2.h>
#include <ds/bitmap.h>
#include <core/panic.h> /* XXX */

int ext2_inode_read(struct ext2 *desc, uint32_t inode, struct ext2_inode *ref)
{
    if (inode >= desc->superblock->inodes_count)    /* Invalid inode */
        return -EINVAL;

    uint32_t block_group = (inode - 1) / desc->superblock->inodes_per_block_group;
    struct ext2_block_group_descriptor *bgd = &desc->bgd_table[block_group];

    uint32_t index = (inode - 1) % desc->superblock->inodes_per_block_group;
    
    if (ref) {
        off_t off = bgd->inode_table * desc->bs + index * desc->superblock->inode_size;
        size_t size = sizeof(struct ext2_inode);
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

struct ext2_inode *ext2_inode_write(struct ext2 *desc, uint32_t inode, struct ext2_inode *i)
{
    if (inode >= desc->superblock->inodes_count)    /* Invalid inode */
        return NULL;

    uint32_t block_group = (inode - 1) / desc->superblock->inodes_per_block_group;
    struct ext2_block_group_descriptor *bgd = &desc->bgd_table[block_group];

    uint32_t index = (inode - 1) % desc->superblock->inodes_per_block_group;
    uint32_t inode_size = desc->superblock->inode_size;
    
    vfs_write(desc->supernode, bgd->inode_table * desc->bs + index * inode_size, inode_size, i);
    return i;
}

size_t ext2_inode_block_read(struct ext2 *desc, struct ext2_inode *inode, size_t idx, void *buf)
{
    size_t bs = desc->bs;
    size_t blocks_nr = (inode->size + bs - 1) / bs;
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx >= blocks_nr) {
        panic("Trying to get invalid block!\n");
    }

    if (idx < EXT2_DIRECT_POINTERS) {
        ext2_block_read(desc, inode->direct_pointer[idx], buf);
    } else if (idx < EXT2_DIRECT_POINTERS + p) {
        uint32_t *tmp = kmalloc(desc->bs);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx - EXT2_DIRECT_POINTERS];
        kfree(tmp);
        ext2_block_read(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

size_t ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf)
{
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx < EXT2_DIRECT_POINTERS) {
        if (!inode->direct_pointer[idx]) {    /* Allocate */
            inode->direct_pointer[idx] = ext2_block_allocate(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }
        ext2_block_write(desc, inode->direct_pointer[idx], buf);
    } else if (idx < EXT2_DIRECT_POINTERS + p) {
        if (!inode->singly_indirect_pointer) {    /* Allocate */
            inode->singly_indirect_pointer = ext2_block_allocate(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }

        uint32_t *tmp = kmalloc(desc->bs);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx - EXT2_DIRECT_POINTERS];

        if (!block) {   /* Allocate */
            tmp[idx - EXT2_DIRECT_POINTERS] = ext2_block_allocate(desc);
            ext2_block_write(desc, inode->singly_indirect_pointer, tmp);
        }

        kfree(tmp);
        ext2_block_write(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

uint32_t ext2_inode_allocate(struct ext2 *desc)
{
    uint32_t *buf = kmalloc(desc->bs);
    uint32_t inode = 0, real_inode = 0, group = 0;
    bitmap_t bm = {0};

    for (unsigned i = 0; i < desc->bgds_count; ++i) {
        if (desc->bgd_table[i].unallocated_inodes_count) {
            ext2_block_read(desc, desc->bgd_table[i].inode_usage_bitmap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->inodes_per_block_group;
            
            /* Look for a free inode */
            for (; bitmap_check(&bm, inode); ++inode);
            group = i;
            real_inode = inode + group * desc->superblock->inodes_per_block_group;
            break;
        }
    }

    if (!inode) {
        /* Out of space */
        kfree(buf);
        return 0;
    }

    //printk("group %d, inode %d, real_inode %d\n", group, inode, real_inode);

    /* Update bitmap */
    bitmap_set(&bm, inode);
    ext2_block_write(desc, desc->bgd_table[group].inode_usage_bitmap, buf);

    /* Update block group descriptor */
    desc->bgd_table[group].unallocated_inodes_count--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->unallocated_inodes_count--;
    ext2_superblock_rewrite(desc);

    kfree(buf);
    return real_inode;
}
