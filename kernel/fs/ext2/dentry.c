#include <ext2.h>
#include <core/panic.h> /* XXX */

uint32_t ext2_dentry_find(struct ext2 *desc, struct ext2_inode *inode, const char *name)
{
    if ((inode->mode & S_IFMT) != S_IFDIR)
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs, &M_BUFFER, 0);
    struct ext2_dentry *d;
    uint32_t inode_nr;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_block_read(desc, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            char _name[d->name_length+1];
            memcpy(_name, d->name, d->name_length);
            _name[d->name_length] = 0;
            if (!strcmp((char *) _name, name))
                goto found;
            d = (struct ext2_dentry *) ((char *) d + d->size);
        }
    }
    
    /* Not found */
    kfree(buf);
    return 0;

found:
    inode_nr = d->inode;
    kfree(buf);
    return inode_nr;
}

int ext2_dentry_create(struct vnode *dir, const char *name, uint32_t inode, uint8_t type)
{
    int err = 0;

    if ((dir->mode & S_IFMT) != S_IFDIR)    /* Not a directory */
        return -ENOTDIR;

    struct ext2 *desc = dir->super->p;

    size_t name_length = strlen(name);
    size_t size = name_length + sizeof(struct ext2_dentry);
    size = (size + 3) & ~3; /* Align to 4 bytes */

    /* Look for a candidate entry */
    char *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct ext2_dentry *cur = NULL;

    struct ext2_inode dir_inode;
    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode))) {
        /* TODO Error checking */
    }

    size_t bs = desc->bs;
    size_t blocks_nr = dir_inode.size / bs;
    size_t flag = 0;    /* 0 => allocate, 1 => replace, 2 => split */
    size_t block = 0;

    for (block = 0; block < blocks_nr; ++block) {
        ext2_inode_block_read(desc, &dir_inode, block, buf);
        cur = (struct ext2_dentry *) buf;

        while ((char *) cur < (char *) buf + bs) {
            if ((!cur->inode) && cur->size >= size) {   /* unused entry */
                flag = 1;   /* Replace */
                goto done;
            }

            if ((((cur->name_length + sizeof(struct ext2_dentry) + 3) & ~3) + size) <= cur->size) {
                flag = 2;   /* Split */
                goto done;
            }

            cur = (struct ext2_dentry *) ((char *) cur + cur->size);
        }
    }

done:
    if (flag == 1) {    /* Replace */
        memcpy(cur->name, name, name_length);
        cur->inode = inode;
        cur->name_length = name_length;
        cur->type = type;
        /* Update block */
        ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else if (flag == 2) { /* Split */
        size_t new_size = (cur->name_length + sizeof(struct ext2_dentry) + 3) & ~3;
        struct ext2_dentry *next = (struct ext2_dentry *) ((char *) cur + new_size);

        next->inode = inode;
        next->size = cur->size - new_size;
        next->name_length = name_length;
        next->type = type;
        memcpy(next->name, name, name_length);

        cur->size = new_size;

        /* Update block */
        ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else {    /* Allocate */
        panic("Not impelemented\n");
    }

    kfree(buf);
    return 0;
}
