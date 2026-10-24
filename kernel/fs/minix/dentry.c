/** \file
 * minix: directory entries, a name and an inode number each, in the
 * directory's blocks.
 */

#include <core/panic.h>
#include <minix.h>

/**
 * Finds the inode number of a name in a directory. The callers check that
 * the inode is a directory: an inode that isn't one has no entries.
 *
 * @param desc     the mounted filesystem
 * @param m_inode  the directory's inode
 * @param name     the name
 * @return the inode number, or 0 if there's no such name (or the inode
 *         isn't a directory)
 */
uint32_t minix_dentry_find(struct minix *desc, struct minix_inode *m_inode, const char *name)
{
    if (!S_ISDIR(m_inode->mode))
        return 0;

    size_t bs = desc->bs;
    size_t nr_blocks = (m_inode->size + bs - 1) / bs;

    char block[bs];

    struct minix_dentry *dentry;

    for (size_t i = 0; i < nr_blocks; ++i) {
        minix_inode_block_read(desc, m_inode, i, block);

        dentry = (struct minix_dentry *) block;

        while ((char *) dentry < (char *) block + bs) {
            char _name[desc->name_len+1];

            memcpy(_name, dentry->name, desc->name_len);
            _name[desc->name_len] = 0;

            if (!strcmp((char *) _name, name))
                goto found;

            dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        }
    }
    
    /* not found */
    return 0;

found:
    return dentry->ino;
}

/**
 * Adds an entry to a directory, in its first free slot. The directory's
 * size grows if the slot is past its end. A directory whose blocks are
 * all full grows by a block, with the entry at its start.
 *
 * @param dir   the directory
 * @param name  the new entry's name (cut to the filesystem's longest)
 * @param ino   its inode number
 * @param mode  its type and permissions (not used)
 * @return 0, `-ENOTDIR` if `dir` isn't a directory, `-ENOSPC` or
 *         `-EFBIG` if it can't grow, or an I/O error
 */
int minix_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode)
{
    int err = 0;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct minix *desc = dir->p;

    size_t len = strlen(name);

    struct minix_inode m_inode;
    if ((err = minix_inode_read(desc, dir->ino, &m_inode)))
        return err;

    size_t bs = desc->bs;
    size_t nr_blocks = (m_inode.size + bs - 1) / bs;

    char block[bs];

    struct minix_dentry *dentry;

    for (size_t i = 0; i < nr_blocks; ++i) {
        minix_inode_block_read(desc, &m_inode, i, block);

        dentry = (struct minix_dentry *) block;

        while ((char *) dentry < (char *) block + bs) {
            if (!dentry->ino) {
                memset(dentry->name, 0, desc->name_len);
                dentry->ino = ino;
                memcpy(dentry->name, name, MIN(len, desc->name_len));
                minix_inode_block_write(desc, &m_inode, dir->ino, i, block);

                /* the directory grows if the entry is past its end; its
                 * vnode's size stays the inode's (a vnode synced with an
                 * older size would cut the new entry off) */
                size_t new_size = i * bs + (size_t)((char *) dentry - block) + desc->dentry_size;
                if (m_inode.size < new_size) {
                    m_inode.size = new_size;
                    dir->size = new_size;
                    return minix_inode_write(desc, dir->ino, &m_inode);
                }

                return 0;
            }

            dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        }
    }

    /* every slot is taken: the directory grows by a block (which
     * minix_inode_block_write() allocates), the entry at its start and
     * the rest of the block free */
    memset(block, 0, bs);

    dentry = (struct minix_dentry *) block;
    dentry->ino = ino;
    memcpy(dentry->name, name, MIN(len, desc->name_len));

    if ((err = minix_inode_block_write(desc, &m_inode, dir->ino, nr_blocks, block)))
        return err;

    m_inode.size = nr_blocks * bs + desc->dentry_size;
    dir->size = m_inode.size;
    return minix_inode_write(desc, dir->ino, &m_inode);
}
