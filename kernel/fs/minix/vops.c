/** \file
 * minix: the vnode operations: reading and writing files, directories,
 * making nodes, and truncating.
 *
 * A file's data moves one block at a time through a kernel buffer: the
 * block's piece of the iobuf is copied into or out of it.
 */

#include <minix.h>
#include <fs/iobuf.h>

/**
 * Reads from a file.
 *
 * @param vnode   the file
 * @param offset  where to read from
 * @param io      where the data goes, and how much is wanted
 * @return the number of bytes read (0 at or past the end), or a negative
 *         errno if nothing was read
 */
ssize_t minix_read(struct vnode *vnode, off_t offset, struct iobuf *io)
{
    int err = 0;

    struct minix *desc = vnode->p;

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, vnode->ino, &m_inode)) < 0)
        return err;

    if ((size_t) offset >= m_inode.size)
        return 0;

    size_t size = MIN(io->resid, (size_t) (m_inode.size - offset));
    char *block = kmalloc(bs, &M_BUFFER, 0);

    if (!block)
        return -ENOMEM;

    size_t done = 0;
    ssize_t r = 0;

    while (done < size) {
        size_t off = (offset + done) % bs;
        size_t len = MIN(bs - off, size - done);

        if ((r = minix_inode_block_read(desc, &m_inode, (offset + done) / bs, block)) < 0)
            break;

        if ((r = iobuf_out(io, block + off, len)) < 0)
            break;

        done += r;

        /* a fault stopped the copy */
        if ((size_t) r < len)
            break;
    }

    kfree(block);
    return done ? (ssize_t) done : r;
}

/**
 * Writes to a file, which grows if the write goes past its end.
 *
 * @param vnode   the file
 * @param offset  where to write
 * @param io      the data, and how much of it
 * @return the number of bytes written, or a negative errno if nothing was
 *         written
 */
ssize_t minix_write(struct vnode *vnode, off_t offset, struct iobuf *io)
{
    int err = 0;

    struct minix *desc = vnode->p;
    size_t size = io->resid, old_size = vnode->size;

    if ((size_t) offset + size > vnode->size) {
        minix_trunc(vnode, (size_t) offset + size);
    }

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, vnode->ino, &m_inode)) < 0)
        return err;

    if ((size_t) offset >= m_inode.size) {
        m_inode.size = offset + size;
        if ((err = minix_inode_write(desc, vnode->ino, &m_inode)) < 0)
            return err;
    }

    size = MIN(size, (size_t) (m_inode.size - offset));
    char *block = kmalloc(bs, &M_BUFFER, 0);

    if (!block)
        return -ENOMEM;

    size_t done = 0;
    ssize_t r = 0;

    while (done < size) {
        size_t off = (offset + done) % bs;
        size_t len = MIN(bs - off, size - done);

        /* writing part of a block: read the rest of it first */
        if ((off || len < bs) &&
            (r = minix_inode_block_read(desc, &m_inode, (offset + done) / bs, block)) < 0)
            break;

        if ((r = iobuf_in(io, block + off, len)) <= 0)
            break;

        ssize_t w = minix_inode_block_write(desc, &m_inode, vnode->ino, (offset + done) / bs, block);

        if (w < 0) {
            r = w;
            break;
        }

        done += r;

        /* a fault stopped the copy */
        if ((size_t) r < len)
            break;
    }

    kfree(block);

    /* if it stopped short, the file ends where the data does */
    if (done < size && (size_t) offset + size > old_size)
        minix_trunc(vnode, MAX(old_size, (size_t) offset + done));

    return done ? (ssize_t) done : r;
}

/**
 * Reads a directory's entry at an offset; unused entries don't count.
 *
 * @param dir     the directory
 * @param off     which entry
 * @param dirent  the entry, filled in
 * @return 1 if an entry was read, 0 past the last one, `-ENOTDIR`, or an
 *         I/O error
 */
ssize_t minix_readdir(struct vnode *dir, off_t off, struct dirent *dirent)
{
    int err = 0;

    struct minix *desc = dir->p;
    size_t bs = desc->bs;
    char block[bs];

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, dir->ino, &m_inode)))
        goto error;

    if (!S_ISDIR(m_inode.mode))
        return -ENOTDIR;

    size_t nr_blocks = (m_inode.size + bs - 1) / bs;

    struct minix_dentry *dentry;
    uint32_t inode_nr;
    off_t idx = 0;

    for (size_t i = 0; i < nr_blocks; ++i) {
        minix_inode_block_read(desc, &m_inode, i, block);

        dentry = (struct minix_dentry *) block;

        while ((char *) dentry < (char *) block + bs) {
            if (dentry->ino) {
                if (idx++ == off) {
                    dirent->d_ino = dentry->ino;

                    memcpy(dirent->d_name, (char *) dentry->name, desc->name_len);
                    dirent->d_name[desc->name_len] = '\0';
                    return 1;
                }
            }

            dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        }
    }

    /* past the last entry */
    return 0;

error:
    return err;
}

/**
 * Makes a node in a directory: a new inode, and an entry for it. A new
 * directory gets its `.` and `..` entries.
 *
 * @param dir   the directory
 * @param fn    the new node's name
 * @param mode  its type (`S_IF*`) and permissions
 * @param dev   its device number, for a device node
 * @param uio   who is asking (the owner)
 * @param ref   if not NULL, set to the new vnode
 * @return 0, `-EINVAL` for an empty name, `-EEXIST`, `-ENOSPC`, or an I/O
 *         error
 */
int minix_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int err = 0;

    if (!fn || !*fn)
        return -EINVAL;

    struct minix *desc = dir->p;
    struct minix_inode dir_inode;
    
    if ((err = minix_inode_read(desc, dir->ino, &dir_inode)))
        goto error;

    if (minix_dentry_find(desc, &dir_inode, fn)) {
        return -EEXIST;
    }

    ino_t ino = minix_inode_alloc(desc);

    if (!ino) { /* out of inodes */
        err = -ENOSPC;
        goto error;
    }

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, ino, &m_inode))) {
        /* not handled: the inode is cleared below anyway */
    }

    memset(&m_inode, 0, sizeof(struct minix_inode));

    m_inode.mode = mode;
    m_inode.uid  = uio->uid;
    m_inode.gid  = uio->gid;

    /* a device node's number, where minix_inode_build() reads it */
    if (S_ISCHR(mode) || S_ISBLK(mode))
        m_inode.zones[0] = DEV(DEV_MAJOR(dev), DEV_MINOR(dev));

    if (S_ISDIR(mode)) {   /* a directory starts with . and .. */
        m_inode.nlinks = 2;
        m_inode.size   = 2 * desc->dentry_size;

        char buf[desc->bs];
        memset(buf, 0, sizeof(buf));

        struct minix_dentry *dentry = (struct minix_dentry *) buf;
        dentry->ino = ino;
        memcpy(dentry->name, ".", 1);

        dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        dentry->ino = dir->ino;
        memcpy(dentry->name, "..", 2);

        minix_inode_block_write(desc, &m_inode, ino, 0, buf);

        /* the parent gets a link, the new directory's .. */
        struct vnode *parent_inode;
        minix_inode_build(desc, dir->ino, &parent_inode);
        parent_inode->nlink += 1;
        minix_inode_sync(parent_inode);
    } else {
        m_inode.nlinks = 1;
        m_inode.size   = 0;
    }

    minix_inode_write(desc, ino, &m_inode);
    minix_dentry_create(dir, fn, ino, m_inode.mode);

    if (ref)
        minix_inode_build(desc, ino, ref);

error:
    return err;
}

/**
 * Finds the entry of a directory that has a name. Only the entry's inode
 * number is filled in.
 *
 * @param dir     the directory
 * @param fn      the name
 * @param dirent  if not NULL, the entry
 * @return 0, `-ENOENT`, or an I/O error
 */
int minix_finddir(struct vnode *dir, const char *fn, struct dirent *dirent)
{
    int err = 0;

    struct minix *desc = dir->p;

    uint32_t id = dir->ino;

    struct minix_inode m_inode;

    if ((err = minix_inode_read(desc, id, &m_inode)))
        goto error;

    ino_t ino = minix_dentry_find(desc, &m_inode, fn);

    if (!ino)
        return -ENOENT;

    if (dirent) {
        dirent->d_ino  = ino;
    }

    return 0;

error:
    return err;
}

/**
 * Gets the vnode of an inode, by its number.
 *
 * @param super  any vnode of the filesystem
 * @param ino    the inode number
 * @param ref    if not NULL, set to the vnode
 * @return 0, or a negative errno
 */
int minix_vget(struct vnode *super, ino_t ino, struct vnode **ref)
{
    struct minix *desc = super->p;

    if (ref) return minix_inode_build(desc, ino, ref);

    return 0;
}

/**
 * Changes a file's length. A file that grows reads zeros past its old end;
 * a file that shrinks frees its zones past the new end.
 *
 * @param vnode  the file
 * @param len    the new length, in bytes
 * @return 0, `-ENOMEM`, or an I/O error
 */
int minix_trunc(struct vnode *vnode, off_t len)
{
    if ((size_t) len == vnode->size)
        return 0;

    /* growing only changes the size: the new part is a hole */
    if ((size_t) len > vnode->size) {
        vnode->size = len;
        minix_inode_sync(vnode);
        return 0;
    }

    /* shrinking: the zones past the new end are freed, and the rest of the
     * last one is zeroed (so the file reads zeros there if it grows
     * again) */
    struct minix *desc = vnode->p;
    struct minix_inode m_inode;
    size_t bs = desc->bs;
    int err;

    if ((err = minix_inode_read(desc, vnode->ino, &m_inode)))
        return err;

    if (len % bs) {
        char *buf = kmalloc(bs, &M_BUFFER, 0);

        if (!buf)
            return -ENOMEM;

        /* a zone that can't be read isn't written over */
        if (!(err = minix_inode_block_read(desc, &m_inode, len / bs, buf))) {
            memset(buf + len % bs, 0, bs - len % bs);
            err = minix_inode_block_write(desc, &m_inode, vnode->ino, len / bs, buf);
        }

        kfree(buf);

        if (err)
            return err;
    }

    minix_zones_free(desc, &m_inode, (len + bs - 1) / bs);

    m_inode.size = len;
    vnode->size = len;
    return minix_inode_write(desc, vnode->ino, &m_inode);
}
