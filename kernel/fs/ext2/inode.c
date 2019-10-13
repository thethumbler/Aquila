#include <ext2.h>
#include <ds/bitmap.h>
#include <core/panic.h>

static inline off_t ext2_inode_off(struct ext2 *desc, ino_t ino)
{
    /* invalid inode */
    if (ino >= desc->superblock->ninodes)
        return -EINVAL;

    uint32_t group_id = (ino - 1) / desc->superblock->ginodes;
    uint32_t index    = (ino - 1) % desc->superblock->ginodes;

    struct ext2_group *group = &desc->groups[group_id];
    return group->inodes * desc->bs + index * desc->superblock->inode_size;
}

int ext2_inode_read(struct ext2 *desc, ino_t ino, struct ext2_inode *ref)
{
    off_t off;
    
    if ((off = ext2_inode_off(desc, ino)) < 0)
        return off;
    
    if (ref) {
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

int ext2_inode_write(struct ext2 *desc, ino_t ino, struct ext2_inode *ext2_inode)
{
    off_t off = ext2_inode_off(desc, ino);
    size_t inode_size = desc->superblock->inode_size;

    if (off < 0)
        return off;
     
    return vfs_write(desc->supernode, off, sizeof(struct ext2_inode), ext2_inode);
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
        return 0;
    }

    idx -= EXT2_DIRECT_POINTERS;
    
    if (idx < p) {
        /* singly indirect */
        uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx];
        kfree(tmp);
        ext2_block_read(desc, block, buf);
        return 0;
    }

    idx -= p;
    
    if (idx < p * p) {
        /* doubly indirect */
        uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        ext2_block_read(desc, inode->doubly_indirect_pointer, tmp);
        uint32_t block = tmp[idx / p];

        ext2_block_read(desc, block, tmp);
        block = tmp[idx % p];
        kfree(tmp);

        ext2_block_read(desc, block, buf);
        return 0;
    }

    panic("Not impelemented");

    /* unreachable */
}

size_t ext2_inode_block_write(struct ext2 *desc, struct ext2_inode *inode, uint32_t inode_nr, size_t idx, void *buf)
{
    size_t p = desc->bs / 4;    /* Pointers per block */

    if (idx < EXT2_DIRECT_POINTERS) {
        if (!inode->direct_pointer[idx]) {    /* Allocate */
            inode->direct_pointer[idx] = ext2_block_alloc(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }
        ext2_block_write(desc, inode->direct_pointer[idx], buf);
    } else if (idx < EXT2_DIRECT_POINTERS + p) {
        if (!inode->singly_indirect_pointer) {    /* Allocate */
            inode->singly_indirect_pointer = ext2_block_alloc(desc);
            ext2_inode_write(desc, inode_nr, inode);
        }

        uint32_t *tmp = kmalloc(desc->bs, &M_BUFFER, 0);
        ext2_block_read(desc, inode->singly_indirect_pointer, tmp);
        uint32_t block = tmp[idx - EXT2_DIRECT_POINTERS];

        if (!block) {   /* Allocate */
            tmp[idx - EXT2_DIRECT_POINTERS] = ext2_block_alloc(desc);
            ext2_block_write(desc, inode->singly_indirect_pointer, tmp);
        }

        kfree(tmp);
        ext2_block_write(desc, block, buf);
    } else {
        panic("Not impelemented\n");
    }

    return 0;
}

ino_t ext2_inode_alloc(struct ext2 *desc)
{
    printk("ext2_inode_alloc(desc=%p)\n", desc);

    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    uint32_t inode = 0, real_inode = 0, group = 0;
    struct bitmap bm = {0};

    for (unsigned i = 0; i < desc->ngroups; ++i) {
        inode = 0;

        if (desc->groups[i].free_inodes) {
            ext2_block_read(desc, desc->groups[i].imap, buf);

            bm.map = buf;
            bm.max_idx = desc->superblock->ginodes;
            
            /* look for a free inode */
            for (; bitmap_check(&bm, inode); ++inode);

            if (inode == bm.max_idx)
                continue;

            group = i;
            real_inode = inode + group * desc->superblock->ginodes + 1;
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
    ext2_block_write(desc, desc->groups[group].imap, buf);

    /* Update block group descriptor */
    desc->groups[group].free_inodes--;
    ext2_bgd_table_rewrite(desc);

    /* Update super block */
    desc->superblock->free_inodes--;
    ext2_superblock_rewrite(desc);

    kfree(buf);
    return real_inode;
}
