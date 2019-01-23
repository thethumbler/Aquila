#include <minix.h>

ssize_t minix_read(struct inode *inode, off_t offset, size_t size, void *buf)
{
    int err = 0;

    struct minix *desc = inode->p;

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    char *read_buf = NULL;
    
    if ((err = minix_inode_read(desc, inode->ino, &m_inode)) < 0)
        goto error;

    if ((size_t) offset >= m_inode.size)
        return 0;

    size = MIN(size, m_inode.size - offset);

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs);

            if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, read_buf)) < 0)
                goto error;

            memcpy(_buf, read_buf + (offset % bs), start);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto done;
        }
    }

    /* Read whole blocks */
    size_t count = size/bs;

    while (count) {
        if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, _buf)) < 0)
            goto error;

        ret    += bs;
        size   -= bs;
        _buf   += bs;
        offset += bs;
        --count;
    }

    if (!size)
        goto done;

    size_t end = size % bs;

    if (end) {
        if (!read_buf)
            read_buf = kmalloc(bs);

        if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, read_buf)) < 0)
            goto error;

        memcpy(_buf, read_buf, end);
        ret += end;
    }

done:
    if (read_buf)
        kfree(read_buf);

    return ret;

error:
    if (read_buf)
        kfree(read_buf);

    return err;
}

ssize_t minix_write(struct inode *inode, off_t offset, size_t size, void *buf)
{
    printk("minix_write(inode=%p, offset=%d, size=%d, buf=%p)\n", inode, offset, size, buf);

    int err = 0;

    struct minix *desc = inode->p;

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    char *write_buf = NULL;
    
    if ((err = minix_inode_read(desc, inode->ino, &m_inode)) < 0)
        goto error;

    if ((size_t) offset >= m_inode.size) {
        m_inode.size = offset + size;
        if ((err = minix_inode_write(desc, inode->ino, &m_inode)) < 0)
            goto error;
    }

    size = MIN(size, m_inode.size - offset);

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs);

            if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, write_buf)) < 0)
                goto error;

            memcpy(write_buf + (offset % bs), _buf, start);

            if ((err = minix_inode_block_write(desc, &m_inode, inode->ino, offset/bs, write_buf)) < 0)
                goto error;

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto done;
        }
    }

    /* Write whole blocks */
    size_t count = size/bs;

    while (count) {
        if ((err = minix_inode_block_write(desc, &m_inode, inode->ino, offset/bs, _buf)) < 0)
            goto error;

        ret    += bs;
        size   -= bs;
        _buf   += bs;
        offset += bs;
        --count;
    }

    if (!size)
        goto done;

    size_t end = size % bs;

    if (end) {
        if (!write_buf)
            write_buf = kmalloc(bs);

        if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, write_buf)) < 0)
            goto error;

        memcpy(write_buf, _buf, end);

        if ((err = minix_inode_block_write(desc, &m_inode, inode->ino, offset/bs, write_buf)) < 0)
            goto error;

        ret += end;
    }

done:
    printk("done\n");
    if (write_buf)
        kfree(write_buf);

    return ret;

error:
    printk("error = %d\n", err);
    if (write_buf)
        kfree(write_buf);

    return err;
}

ssize_t minix_readdir(struct inode *dir, off_t off, struct dirent *dirent)
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
            if (dentry->inode) {
                if (idx++ == off) {
                    dirent->d_ino = dentry->inode;

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

int minix_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref)
{
    printk("minix_vmknod(dir=%p, fn=%s, mode=%d, dev=%x, uio=%p, ref=%p)\n", dir, fn, mode, dev, uio, ref);

    int err = 0;

    if (!fn || !*fn)
        return -EINVAL;

    struct minix *desc = dir->super->p;
    struct minix_inode dir_inode;
    
    if ((err = minix_inode_read(desc, dir->ino, &dir_inode)))
        goto error;

    if (minix_dentry_find(desc, &dir_inode, fn)) {
        /* File exists */
        return -EEXIST;
    }

    uint32_t inode_id = minix_inode_allocate(desc);

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, inode_id, &m_inode))) {
        /* TODO Error checking */
    }

    memset(&m_inode, 0, sizeof(struct minix_inode));

    m_inode.mode = mode;
    m_inode.uid  = uio->uid;
    m_inode.gid  = uio->gid;

    if (S_ISDIR(mode)) {   /* Initalize directory structure */
        m_inode.nlinks = 2;
        m_inode.size   = 2 * desc->dentry_size;

        char buf[desc->bs];
        memset(buf, 0, sizeof(buf));

        struct minix_dentry *dentry = (struct minix_dentry *) buf;
        dentry->inode = inode_id;
        memcpy(dentry->name, ".", 1);

        dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        dentry->inode = dir->ino;
        memcpy(dentry->name, "..", 2);

        minix_inode_block_write(desc, &m_inode, inode_id, 0, buf);
    } else {
        m_inode.nlinks = 1;
        m_inode.size   = 0;
    }

    minix_inode_write(desc, inode_id, &m_inode);
    minix_dentry_create(dir, fn, inode_id, m_inode.mode);

    if (ref)
        minix_inode_build(desc, inode_id, ref);

error:
    return 0;
}

int minix_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    int err = 0;

    struct minix *desc = dir->super->p;

    uint32_t id = dir->ino;

    struct minix_inode m_inode;

    if ((err = minix_inode_read(desc, id, &m_inode)))
        goto error;

    uint32_t inode_id = minix_dentry_find(desc, &m_inode, fn);

    if (!inode_id)  /* Not found */
        return -ENOENT;

    if (child) {
        child->ino  = inode_id;
        child->mode = m_inode.mode;
    }

    return 0;

error:
    return err;
}

int minix_vget(struct vnode *vnode, struct inode **ref)
{
    struct minix *desc = vnode->super->p;

    if (ref) return minix_inode_build(desc, vnode->ino, ref);

    return 0;
}
