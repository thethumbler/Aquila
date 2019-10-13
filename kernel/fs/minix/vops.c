#include <minix.h>

ssize_t minix_read(struct vnode *vnode, off_t offset, size_t size, void *buf)
{
    //printk("minix_read(vnode=%p, offset=%d, size=%d, buf=%p)\n", vnode, offset, size, buf);

    int err = 0;

    struct minix *desc = vnode->p;

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    char *read_buf = NULL;
    
    if ((err = minix_inode_read(desc, vnode->ino, &m_inode)) < 0)
        goto error;

    if ((size_t) offset >= m_inode.size)
        return 0;

    size = MIN(size, (size_t) (m_inode.size - offset));

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs, &M_BUFFER, 0);

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
            read_buf = kmalloc(bs, &M_BUFFER, 0);

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

ssize_t minix_write(struct vnode *vnode, off_t offset, size_t size, void *buf)
{
    //printk("minix_write(vnode=%p, offset=%d, size=%d, buf=%p)\n", vnode, offset, size, buf);

    int err = 0;

    struct minix *desc = vnode->p;

    if ((size_t) offset + size > vnode->size) {
        minix_trunc(vnode, (size_t) offset + size);
    }

    size_t bs = desc->bs;
    struct minix_inode m_inode;
    char *write_buf = NULL;
    
    if ((err = minix_inode_read(desc, vnode->ino, &m_inode)) < 0)
        goto error;

    if ((size_t) offset >= m_inode.size) {
        m_inode.size = offset + size;
        if ((err = minix_inode_write(desc, vnode->ino, &m_inode)) < 0)
            goto error;
    }

    size = MIN(size, (size_t) (m_inode.size - offset));

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs, &M_BUFFER, 0);

            if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, write_buf)) < 0)
                goto error;

            memcpy(write_buf + (offset % bs), _buf, start);

            if ((err = minix_inode_block_write(desc, &m_inode, vnode->ino, offset/bs, write_buf)) < 0)
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
        if ((err = minix_inode_block_write(desc, &m_inode, vnode->ino, offset/bs, _buf)) < 0)
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
            write_buf = kmalloc(bs, &M_BUFFER, 0);

        if ((err = minix_inode_block_read(desc, &m_inode, offset/bs, write_buf)) < 0)
            goto error;

        memcpy(write_buf, _buf, end);

        if ((err = minix_inode_block_write(desc, &m_inode, vnode->ino, offset/bs, write_buf)) < 0)
            goto error;

        ret += end;
    }

done:
    if (write_buf)
        kfree(write_buf);

    return ret;

error:
    if (write_buf)
        kfree(write_buf);

    return err;
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
    //printk("minix_vmknod(dir=%p, fn=%s, mode=%d, dev=%x, uio=%p, ref=%p)\n", dir, fn, mode, dev, uio, ref);

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

    struct minix_inode m_inode;
    
    if ((err = minix_inode_read(desc, ino, &m_inode))) {
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
        dentry->ino = ino;
        memcpy(dentry->name, ".", 1);

        dentry = (struct minix_dentry *) ((char *) dentry + desc->dentry_size);
        dentry->ino = dir->ino;
        memcpy(dentry->name, "..", 2);

        minix_inode_block_write(desc, &m_inode, ino, 0, buf);

        /* update links count to parent directory */
        struct vnode *parent_inode;
        minix_inode_build(desc, dir->ino, &parent_inode);
        //printk("parent vnode links %d\n", parent_inode->nlink);
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
    return 0;
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

    /* otherwise the new size is smaller, we need to free the extra blocks */
    /* TODO */


#if 0
    blk_t old_blocks = (vnode->size  + desc->bs - 1) / desc->bs;
    blk_t new_blocks = ((size_t) len + desc->bs - 1) / desc->bs;

    for (blk_t blk = old_blocks + 1; blk < new_blocks; ++blk) {
        printk("should free block %d\n", blk);
        //minix_inode_block(struct minix *desc, ino_t ino, blk_t blk)
    }
#endif

    vnode->size = len;
    minix_inode_sync(vnode);
    return 0;
}
