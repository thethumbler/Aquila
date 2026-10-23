#include <minix.h>
#include <fs/iobuf.h>

/*
 * A file's data moves a block at a time through a kernel buffer: the
 * block's piece of the iobuf is copied in or out of it.
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

        /* a fault stopped it */
        if ((size_t) r < len)
            break;
    }

    kfree(block);
    return done ? (ssize_t) done : r;
}

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

        /* part of a block: the rest of it stays */
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

        /* a fault stopped it */
        if ((size_t) r < len)
            break;
    }

    kfree(block);

    /* it stopped short: the file ends where the data does */
    if (done < size && (size_t) offset + size > old_size)
        minix_trunc(vnode, MAX(old_size, (size_t) offset + done));

    return done ? (ssize_t) done : r;
}

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

    /* not found */
    return 0;

error:
    return err;
}

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
        /* File exists */
        return -EEXIST;
    }

    ino_t ino = minix_inode_alloc(desc);

    if (!ino) { /* out of inodes */
        err = -ENOSPC;
        goto error;
    }

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, ino, &m_inode))) {
        /* TODO Error checking */
    }

    memset(&m_inode, 0, sizeof(struct minix_inode));

    m_inode.mode = mode;
    m_inode.uid  = uio->uid;
    m_inode.gid  = uio->gid;

    /* a device node's number, where minix_inode_build() reads it */
    if (S_ISCHR(mode) || S_ISBLK(mode))
        m_inode.zones[0] = DEV(DEV_MAJOR(dev), DEV_MINOR(dev));

    if (S_ISDIR(mode)) {   /* Initalize directory structure */
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

        /* update links count to parent directory */
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

int minix_finddir(struct vnode *dir, const char *fn, struct dirent *dirent)
{
    int err = 0;

    struct minix *desc = dir->p;

    uint32_t id = dir->ino;

    struct minix_inode m_inode;

    if ((err = minix_inode_read(desc, id, &m_inode)))
        goto error;

    ino_t ino = minix_dentry_find(desc, &m_inode, fn);

    if (!ino)  /* Not found */
        return -ENOENT;

    if (dirent) {
        dirent->d_ino  = ino;
    }

    return 0;

error:
    return err;
}

int minix_vget(struct vnode *super, ino_t ino, struct vnode **ref)
{
    struct minix *desc = super->p;

    if (ref) return minix_inode_build(desc, ino, ref);

    return 0;
}

int minix_trunc(struct vnode *vnode, off_t len)
{
    /* do nothing */
    if ((size_t) len == vnode->size)
        return 0;

    /* just extend vnode size */
    if ((size_t) len > vnode->size) {
        vnode->size = len;
        minix_inode_sync(vnode);
        return 0;
    }

    /* smaller: the zones past the new end are freed, the rest of the last
     * one zeroed (the file reads zeros there if it grows again) */
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
