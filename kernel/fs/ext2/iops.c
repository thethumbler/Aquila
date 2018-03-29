#include <ext2.h>

ssize_t ext2_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    struct __ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode *inode = ext2_inode_read(desc, node->id);

    if ((size_t) offset >= inode->size)
        return 0;

    size = MIN(size, inode->size - offset);
    
    char *read_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs);
            ext2_inode_block_read(desc, inode, offset/bs, read_buf);
            memcpy(_buf, read_buf + (offset % bs), start);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto free_resources;
        }
    }

    /* Read whole blocks */
    size_t count = size/bs;

    while (count) {
        ext2_inode_block_read(desc, inode, offset/bs, _buf);

        ret    += bs;
        size   -= bs;
        _buf   += bs;
        offset += bs;
        --count;
    }

    if (!size)
        goto free_resources;

    size_t end = size % bs;

    if (end) {
        if (!read_buf)
            read_buf = kmalloc(bs);

        ext2_inode_block_read(desc, inode, offset/bs, read_buf);
        memcpy(_buf, read_buf, end);
        ret += end;
    }

free_resources:
    kfree(inode);
    if (read_buf)
        kfree(read_buf);
    return ret;
}

ssize_t ext2_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    struct __ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode *inode = ext2_inode_read(desc, node->id);

    if ((size_t) offset + size > inode->size) {
        inode->size = offset + size;
        ext2_inode_write(desc, node->id, inode);
    }

    size = MIN(size, inode->size - offset);
    
    char *write_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs);
            ext2_inode_block_read(desc, inode, offset/bs, write_buf);
            memcpy(write_buf + (offset % bs), _buf, start);
            ext2_inode_block_write(desc, inode, node->id, offset/bs, write_buf);

            ret += start;
            size -= start;
            _buf += start;
            offset += start;

            if (!size)
                goto free_resources;
        }
    }

    /* Write whole blocks */
    size_t count = size/bs;

    while (count) {
        ext2_inode_block_write(desc, inode, node->id, offset/bs, _buf);

        ret    += bs;
        size   -= bs;
        _buf   += bs;
        offset += bs;
        --count;
    }

    if (!size)
        goto free_resources;

    size_t end = size % bs;

    if (end) {
        if (!write_buf)
            write_buf = kmalloc(bs);

        ext2_inode_block_read(desc, inode, offset/bs, write_buf);
        memcpy(write_buf, _buf, end);
        ext2_inode_block_write(desc, inode, node->id, offset/bs, write_buf);
        ret += end;
    }

free_resources:
    kfree(inode);
    if (write_buf)
        kfree(write_buf);
    return ret;
}

ssize_t ext2_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    struct __ext2 *desc = dir->p;
    struct ext2_inode *inode = ext2_inode_read(desc, dir->id);

    if (inode->type != EXT2_INODE_TYPE_DIR) {
        kfree(inode);
        return -ENOTDIR;
    }

    size_t bs = desc->bs;
    size_t blocks_nr = inode->size / bs;

    char *buf = kmalloc(bs);
    struct ext2_dentry *d;
    off_t idx = 0;
    char found = 0;

    for (size_t i = 0; i < blocks_nr; ++i) {
        ext2_inode_block_read(desc, inode, i, buf);
        d = (struct ext2_dentry *) buf;
        while ((char *) d < (char *) buf + bs) {
            if (idx == offset) {
                dirent->d_ino = d->inode;
                memcpy(dirent->d_name, (char *) d->name, d->name_length);
                dirent->d_name[d->name_length] = '\0';
                found = 1;
                break;
            }

            d = (struct ext2_dentry *) ((char *) d + d->size);
            ++idx;
        }
    }

    kfree(buf);
    kfree(inode);

    return found;
}


int ext2_vmknod(struct vnode *dir, const char *fn, itype_t type, dev_t dev, struct uio *uio, struct inode **ref)
{
    printk("ext2_vmknod(dir=%p, fn=%s, type=%d, dev=%x, uio=%p, ref=%p)\n", dir, fn, type, dev, uio, ref);

    if (!fn || !*fn)
        return -EINVAL;

    struct __ext2 *desc = dir->super->p;
    struct ext2_inode *dir_inode = ext2_inode_read(desc, dir->id);

    if (!dir_inode) /* Invalid inode? */
        return -ENOTDIR;

    if (ext2_dentry_find(desc, dir_inode, fn)) {
        /* File exists */
        kfree(dir_inode);
        return -EEXIST;
    }

    uint32_t inode_nr = ext2_inode_allocate(desc);
    struct ext2_inode *inode = ext2_inode_read(desc, inode_nr);

    memset(inode, 0, sizeof(*inode));

    switch (type) {
        case FS_RGL:
            inode->type = EXT2_INODE_TYPE_RGL;
            break;
        case FS_DIR:
            inode->type = EXT2_INODE_TYPE_DIR;
            break;
        case FS_CHRDEV:
            inode->type = EXT2_INODE_TYPE_CHR;
            break;
        case FS_BLKDEV:
            inode->type = EXT2_INODE_TYPE_BLK;
            break;
        case FS_FIFO:
            inode->type = EXT2_INODE_TYPE_FIFO;
            break;
    }

    if (type == FS_DIR) {   /* Initalize directory structure */
        inode->hard_links_count = 2;
        inode->size = desc->bs;
        
        char *buf = kmalloc(desc->bs);
        struct ext2_dentry *d = (struct ext2_dentry *) buf;
        d->inode = inode_nr;
        d->size = 12;
        d->name_length = 1;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, ".", 1);
        d = (struct ext2_dentry *) ((char *) d + 12);
        d->inode = dir->id;
        d->size = desc->bs - 12;
        d->name_length = 2;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, "..", 2);
        ext2_inode_block_write(desc, inode, inode_nr, 0, buf);
        kfree(buf);
    } else {
        inode->hard_links_count = 1;
        ext2_inode_write(desc, inode_nr, inode);
    }

    ext2_dentry_create(dir, fn, inode_nr, inode->type);

    kfree(dir_inode);
    kfree(inode);

    if (ref)
        ext2_inode_build(desc, inode_nr, ref);

    return 0;
}


int ext2_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    printk("ext2_vfind(dir=%p, fn=%s, child=%p)\n", dir, fn, child);

    struct __ext2 *desc = dir->super->p;

    uint32_t inode = dir->id;

    struct ext2_inode *i = ext2_inode_read(desc, inode);
    uint32_t inode_nr = ext2_dentry_find(desc, i, fn);
    kfree(i);

    if (!inode_nr)  /* Not found */
        return -ENOENT;

    inode = inode_nr;

    if (child) {
        child->id   = inode_nr;
        switch (i->type) {
            case EXT2_INODE_TYPE_FIFO:  child->type = FS_FIFO; break;
            case EXT2_INODE_TYPE_CHR:   child->type = FS_CHRDEV; break;
            case EXT2_INODE_TYPE_DIR:   child->type = FS_DIR; break;
            case EXT2_INODE_TYPE_BLK:   child->type = FS_BLKDEV; break;
            case EXT2_INODE_TYPE_RGL:   child->type = FS_RGL; break;
            case EXT2_INODE_TYPE_SLINK: child->type = FS_SYMLINK; break;
            case EXT2_INODE_TYPE_SCKT:  child->type = FS_SOCKET; break;
        }
    }

    return 0;
}

int ext2_vget(struct vnode *vnode, struct inode **ref)
{
    struct __ext2 *desc = vnode->super->p;

    if (ref) {
        ext2_inode_build(desc, vnode->id, ref);
    }

    return 0;
}
