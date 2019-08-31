#include <ext2.h>

ssize_t ext2_read(struct inode *node, off_t offset, size_t size, void *buf)
{
    int err = 0;

    struct ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, node->ino, &inode))) {
        /* TODO Error checking */
    }

    if ((size_t) offset >= inode.size)
        return 0;

    size = MIN(size, (size_t) (inode.size - offset));
    
    char *read_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Read up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            read_buf = kmalloc(bs, &M_BUFFER, 0);
            ext2_inode_block_read(desc, &inode, offset/bs, read_buf);
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
        ext2_inode_block_read(desc, &inode, offset/bs, _buf);

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
            read_buf = kmalloc(bs, &M_BUFFER, 0);

        ext2_inode_block_read(desc, &inode, offset/bs, read_buf);
        memcpy(_buf, read_buf, end);
        ret += end;
    }

free_resources:
    if (read_buf)
        kfree(read_buf);
    return ret;
}

ssize_t ext2_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    int err = 0;
    struct ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, node->ino, &inode))) {

    }

    if ((size_t) offset + size > inode.size) {
        inode.size = offset + size;
        ext2_inode_write(desc, node->ino, &inode);
    }

    size = MIN(size, (size_t) (inode.size - offset));
    
    char *write_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs, &M_BUFFER, 0);
            ext2_inode_block_read(desc, &inode, offset/bs, write_buf);
            memcpy(write_buf + (offset % bs), _buf, start);
            ext2_inode_block_write(desc, &inode, node->ino, offset/bs, write_buf);

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
        ext2_inode_block_write(desc, &inode, node->ino, offset/bs, _buf);

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
            write_buf = kmalloc(bs, &M_BUFFER, 0);

        ext2_inode_block_read(desc, &inode, offset/bs, write_buf);
        memcpy(write_buf, _buf, end);
        ext2_inode_block_write(desc, &inode, node->ino, offset/bs, write_buf);
        ret += end;
    }

free_resources:
    if (write_buf)
        kfree(write_buf);
    return ret;
}

ssize_t ext2_readdir(struct inode *dir, off_t offset, struct dirent *dirent)
{
    int err = 0;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct ext2 *desc = dir->p;
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, dir->ino, &inode))) {
        /* TODO Error */
        printk("err = %d\n", err);
    }

    if (!S_ISDIR(inode.mode))
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode.size / bs;

    char buf[bs];

    struct ext2_dentry *dentry;
    off_t idx = 0;
    char found = 0;

    for (size_t i = 0; i < blocks_nr; ++i) {

        ext2_inode_block_read(desc, &inode, i, buf);
        dentry = (struct ext2_dentry *) buf;

        while ((char *) dentry < (char *) buf + bs) {
            if (idx == offset) {
                dirent->d_ino = dentry->inode;
                memcpy(dirent->d_name, (char *) dentry->name, dentry->name_length);
                dirent->d_name[dentry->name_length] = '\0';
                found = 1;
                break;
            }

            dentry = (struct ext2_dentry *) ((char *) dentry + dentry->size);
            ++idx;
        }
    }

    return found;
}

int ext2_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref)
{
    int err = 0;

    if (!fn || !*fn)
        return -EINVAL;

    struct ext2 *desc = dir->super->p;
    struct ext2_inode dir_inode;
    
    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode))) {
        /* TODO Error checking */
    }

    if (ext2_dentry_find(desc, &dir_inode, fn)) {
        /* File exists */
        return -EEXIST;
    }

    uint32_t inode_nr = ext2_inode_allocate(desc);
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, inode_nr, &inode))) {
        /* TODO Error checking */
    }

    memset(&inode, 0, sizeof(struct ext2_inode));

    inode.mode = mode;

    if (S_ISDIR(mode)) {   /* Initalize directory structure */
        inode.nlinks = 2;
        inode.size = desc->bs;
        
        char *buf = kmalloc(desc->bs, &M_BUFFER, 0);
        struct ext2_dentry *d = (struct ext2_dentry *) buf;
        d->inode = inode_nr;
        d->size = 12;
        d->name_length = 1;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, ".", 1);
        d = (struct ext2_dentry *) ((char *) d + 12);
        d->inode = dir->ino;
        d->size = desc->bs - 12;
        d->name_length = 2;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, "..", 2);
        ext2_inode_block_write(desc, &inode, inode_nr, 0, buf);
        kfree(buf);
    } else {
        inode.nlinks = 1;
        ext2_inode_write(desc, inode_nr, &inode);
    }

    ext2_dentry_create(dir, fn, inode_nr, inode.mode);

    if (ref)
        ext2_inode_build(desc, inode_nr, ref);

    return 0;
}


int ext2_vfind(struct vnode *dir, const char *fn, struct vnode *child)
{
    //printk("ext2_vfind(dir=%p, fn=%s, child=%p)\n", dir, fn, child);

    int err = 0;

    struct ext2 *desc = dir->super->p;

    uint32_t inode = dir->ino;

    struct ext2_inode i;
    if ((err = ext2_inode_read(desc, inode, &i))) {
        /* TODO Error checking */
    }

    uint32_t inode_nr = ext2_dentry_find(desc, &i, fn);

    if (!inode_nr)  /* Not found */
        return -ENOENT;

    inode = inode_nr;

    if (child) {
        child->ino  = inode_nr;
        child->mode = i.mode;
    }

    return 0;
}

int ext2_vget(struct vnode *vnode, struct inode **ref)
{
    struct ext2 *desc = vnode->super->p;

    if (ref) {
        ext2_inode_build(desc, vnode->ino, ref);
    }

    return 0;
}
