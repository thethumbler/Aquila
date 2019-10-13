#include <ext2.h>
#include <core/panic.h> /* XXX */

ino_t ext2_dentry_find(struct ext2 *desc, struct ext2_inode *inode, const char *name)
{
    if ((inode->mode & S_IFMT) != S_IFDIR)
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs, &M_BUFFER, 0);
    struct ext2_dentry *d;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_block_read(desc, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            char _name[d->length+1];
            memcpy(_name, d->name, d->length);
            _name[d->length] = 0;
            if (!strcmp((char *) _name, name))
                goto found;
            d = (struct ext2_dentry *) ((char *) d + d->size);
        }
    }
    
    /* Not found */
    kfree(buf);
    return 0;

found:
    kfree(buf);
    return d->ino;
}

static inline uint8_t ext2_dentry_type(mode_t mode)
{
    switch (mode & S_IFMT) {
        case S_IFSOCK: return EXT2_DENTRY_TYPE_SCKT;
        case S_IFLNK:  return EXT2_DENTRY_TYPE_SLINK;
        case S_IFREG:  return EXT2_DENTRY_TYPE_RGL;
        case S_IFBLK:  return EXT2_DENTRY_TYPE_BLK;
        case S_IFDIR:  return EXT2_DENTRY_TYPE_DIR;
        case S_IFCHR:  return EXT2_DENTRY_TYPE_CHR;
        case S_IFIFO:  return EXT2_DENTRY_TYPE_FIFO;
        default:       return EXT2_DENTRY_TYPE_BAD;
    }
}

int ext2_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode)
{
    int err = 0;

    /* not a directory */
    if ((dir->mode & S_IFMT) != S_IFDIR)
        return -ENOTDIR;

    struct ext2 *desc = dir->p;

    uint8_t type = ext2_dentry_type(mode);
    size_t length = strlen(name);
    size_t size = length + sizeof(struct ext2_dentry);
    size = (size + 3) & ~3; /* Align to 4 bytes */

    /* look for a candidate entry */
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
            if ((!cur->ino) && cur->size >= size) {   /* unused entry */
                flag = 1;   /* Replace */
                goto done;
            }

            if ((((cur->length + sizeof(struct ext2_dentry) + 3) & ~3) + size) <= cur->size) {
                flag = 2;   /* Split */
                goto done;
            }

            cur = (struct ext2_dentry *) ((char *) cur + cur->size);
        }
    }

done:
    if (flag == 1) {
        /* replace */
        memcpy(cur->name, name, length);
        cur->ino    = ino;
        cur->length = length;
        cur->type   = type;

        /* Update block */
        ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else if (flag == 2) {
        /* split */
        size_t new_size = (cur->length + sizeof(struct ext2_dentry) + 3) & ~3;
        struct ext2_dentry *next = (struct ext2_dentry *) ((char *) cur + new_size);

        next->ino    = ino;
        next->size   = cur->size - new_size;
        next->length = length;
        next->type   = type;
        memcpy(next->name, name, length);

        cur->size = new_size;

        /* Update block */
        ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else {
        /* allocate */
        panic("Not impelemented\n");
    }

    kfree(buf);
    return 0;
}
