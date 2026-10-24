/** \file
 * ext2: directory entries. A directory's blocks hold entries of different
 * sizes, each saying where the next one is (its `size`); an entry with no
 * inode is unused, and a new entry takes an unused one, the room left at
 * the end of another, or a new block.
 */

#include <ext2.h>

/**
 * Finds the inode number of a name in a directory.
 *
 * @param desc   the mounted filesystem
 * @param inode  the directory's inode
 * @param name   the name
 * @param ino    set to the inode number
 * @return 0, `-ENOENT` if there's no such name, `-ENOTDIR`, `-ENOMEM`, or
 *         an I/O error
 */
int ext2_dentry_find(struct ext2 *desc, struct ext2_inode *inode, const char *name, ino_t *ino)
{
    if ((inode->mode & S_IFMT) != S_IFDIR)
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;
    size_t length = strlen(name);

    char *buf = kmalloc(bs, &M_BUFFER, 0);
    struct ext2_dentry *d;
    int err = -ENOENT;

    if (!buf)
        return -ENOMEM;

    for (size_t i = 0; i < blocks_nr && err == -ENOENT; ++i) {
        int r;

        if ((r = ext2_inode_block_read(desc, inode, i, buf))) {
            err = r;
            break;
        }

        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs && d->size) {
            /* entries with no inode are unused */
            if (d->ino && d->length == length && !memcmp(d->name, name, length)) {
                /* read it before the buffer is freed */
                *ino = d->ino;
                err = 0;
                break;
            }
            d = (struct ext2_dentry *) ((char *) d + d->size);
        }
    }

    kfree(buf);
    return err;
}

/** Returns the directory entry type of a file's mode. */
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

/**
 * Adds an entry to a directory. It takes an unused entry big enough, or
 * splits an entry that has room after its name, or else gets a new block
 * at the end of the directory.
 *
 * @param dir   the directory
 * @param name  the new entry's name
 * @param ino   its inode number
 * @param mode  its file's mode (for the entry's type)
 * @return 0, `-ENOTDIR`, `-ENOMEM`, or an I/O error
 */
int ext2_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode)
{
    int err = 0;

    /* not a directory */
    if ((dir->mode & S_IFMT) != S_IFDIR)
        return -ENOTDIR;

    struct ext2 *desc = dir->p;

    uint8_t type = desc->filetype ? ext2_dentry_type(mode) : 0;
    size_t length = strlen(name);
    size_t size = length + sizeof(struct ext2_dentry);
    size = (size + 3) & ~3; /* entries are aligned to 4 bytes */

    /* look for room for the entry */
    struct ext2_inode dir_inode;

    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode)))
        return err;

    char *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct ext2_dentry *cur = NULL;

    if (!buf)
        return -ENOMEM;

    size_t bs = desc->bs;
    size_t blocks_nr = dir_inode.size / bs;
    size_t flag = 0;    /* 0: a new block, 1: an unused entry, 2: a split */
    size_t block = 0;

    for (block = 0; block < blocks_nr; ++block) {
        /* a block that can't be read isn't written back */
        if ((err = ext2_inode_block_read(desc, &dir_inode, block, buf)))
            goto out;

        cur = (struct ext2_dentry *) buf;

        while ((char *) cur < (char *) buf + bs && cur->size) {
            /* an unused entry big enough */
            if ((!cur->ino) && cur->size >= size) {
                flag = 1;
                goto done;
            }

            /* an entry with room for the new one after its name */
            if ((((cur->length + sizeof(struct ext2_dentry) + 3) & ~3) + size) <= cur->size) {
                flag = 2;
                goto done;
            }

            cur = (struct ext2_dentry *) ((char *) cur + cur->size);
        }
    }

done:
    if (flag == 1) {
        /* take the unused entry */
        memcpy(cur->name, name, length);
        cur->ino    = ino;
        cur->length = length;
        cur->type   = type;

        err = ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else if (flag == 2) {
        /* split the entry: the new one takes the room after its name */
        size_t new_size = (cur->length + sizeof(struct ext2_dentry) + 3) & ~3;
        struct ext2_dentry *next = (struct ext2_dentry *) ((char *) cur + new_size);

        next->ino    = ino;
        next->size   = cur->size - new_size;
        next->length = length;
        next->type   = type;
        memcpy(next->name, name, length);

        cur->size = new_size;

        err = ext2_inode_block_write(desc, &dir_inode, dir->ino, block, buf);
    } else {
        /* no room left: the entry gets a new block of its own */
        memset(buf, 0, bs);
        cur = (struct ext2_dentry *) buf;
        cur->ino    = ino;
        cur->size   = bs;
        cur->length = length;
        cur->type   = type;
        memcpy(cur->name, name, length);

        if ((err = ext2_inode_block_write(desc, &dir_inode, dir->ino, blocks_nr, buf)))
            goto out;

        dir_inode.size += bs;
        err = ext2_inode_write(desc, dir->ino, &dir_inode);
        dir->size = dir_inode.size;
    }

out:
    kfree(buf);
    return err;
}
