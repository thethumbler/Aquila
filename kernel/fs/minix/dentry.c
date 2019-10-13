#include <core/panic.h>
#include <minix.h>

uint32_t minix_dentry_find(struct minix *desc, struct minix_inode *m_inode, const char *name)
{
    if (!S_ISDIR(m_inode->mode))
        return -ENOTDIR;

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

int minix_dentry_create(struct vnode *dir, const char *name, ino_t ino, mode_t mode)
{
    int err = 0;

    if (!S_ISDIR(dir->mode))    /* Not a directory */
        return -ENOTDIR;

    struct minix *desc = dir->p;

    size_t len = strlen(name);

    struct minix_inode m_inode;
    if ((err = minix_inode_read(desc, dir->ino, &m_inode))) {
        /* TODO Error checking */
    }

    size_t bs = desc->bs;
    size_t nr_blocks = (m_inode.size + bs - 1) / bs;

    char block[bs];

    struct minix_dentry *dentry;

    for (size_t i = 0; i < nr_blocks; ++i) {
        minix_inode_block_read(desc, &m_inode, i, block);

        dentry = (struct minix_dentry *) block;

        while ((char *) dentry < (char *) block + bs) {
            if (!dentry->ino) {
                memset(dentry->name, 0, desc->dentry_size);
                dentry->ino = ino;
                memcpy(dentry->name, name, MIN(len, desc->name_len));
                minix_inode_block_write(desc, &m_inode, dir->ino, i, block);

                /* update size */
                size_t new_size = i * bs + (size_t)((char *) dentry - block) + desc->dentry_size;
                if (m_inode.size < new_size) {
                    m_inode.size = new_size;
                    minix_inode_write(desc, dir->ino, &m_inode);
                }

                return 0;
            }

            dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        }
    }

    /* TOOD allocate */
    panic("Need to allocate a new block\n");
}
