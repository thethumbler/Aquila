#include <ext2.h>
#include <core/time.h>

ssize_t ext2_read(struct vnode *node, off_t offset, size_t size, void *buf)
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

ssize_t ext2_write(struct vnode *node, off_t offset, size_t size, void *buf)
{
    printk("ext2_write(inode=%p, offset=%d, size=%d, buf=%p)\n", node, offset, size, buf);

    int err = 0;
    struct ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode ext2_inode;
    
    if ((err = ext2_inode_read(desc, node->ino, &ext2_inode))) {

    }

    if ((size_t) offset + size > ext2_inode.size) {
        ext2_inode.size = (size_t) offset + size;
        ext2_trunc(node, (size_t) offset + size);
    }

    size = MIN(size, (size_t) (ext2_inode.size - offset));
    
    char *write_buf = NULL;

    ssize_t ret = 0;
    char *_buf = buf;

    /* Write up to block boundary */
    if (offset % bs) {
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            write_buf = kmalloc(bs, &M_BUFFER, 0);
            ext2_inode_block_read(desc, &ext2_inode, offset/bs, write_buf);
            memcpy(write_buf + (offset % bs), _buf, start);
            ext2_inode_block_write(desc, &ext2_inode, node->ino, offset/bs, write_buf);

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
        ext2_inode_block_write(desc, &ext2_inode, node->ino, offset/bs, _buf);

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

        ext2_inode_block_read(desc, &ext2_inode, offset/bs, write_buf);
        memcpy(write_buf, _buf, end);
        ext2_inode_block_write(desc, &ext2_inode, node->ino, offset/bs, write_buf);
        ret += end;
    }

free_resources:
    if (write_buf)
        kfree(write_buf);

    return ret;
}

ssize_t ext2_readdir(struct vnode *dir, off_t offset, struct dirent *dirent)
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
                dirent->d_ino = dentry->ino;
                memcpy(dirent->d_name, (char *) dentry->name, dentry->length);
                dirent->d_name[dentry->length] = '\0';
                found = 1;
                break;
            }

            dentry = (struct ext2_dentry *) ((char *) dentry + dentry->size);
            ++idx;
        }
    }

    return found;
}

int ext2_trunc(struct vnode *inode, off_t len)
{
    /* do nothing */
    if ((size_t) len == inode->size)
        return 0;

    /* just extend inode size */
    if ((size_t) len > inode->size) {
        inode->size = len;
        ext2_inode_sync(inode);
        return 0;
    }

    /* otherwise the new size is smaller, we need to free the extra blocks */
    /* TODO */

    inode->size = len;
    ext2_inode_sync(inode);
    return 0;
}

int ext2_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int err = 0;

    if (!fn || !*fn)
        return -EINVAL;

    struct ext2 *desc = dir->p;
    struct ext2_inode dir_inode;
    
    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode))) {
        /* TODO Error checking */
    }

    /* file exists */
    if (ext2_dentry_find(desc, &dir_inode, fn))
        return -EEXIST;

    ino_t ino = ext2_inode_alloc(desc);
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, ino, &inode))) {
        /* TODO Error checking */
    }

    memset(&inode, 0, sizeof(struct ext2_inode));

    inode.mode = mode;

    struct timespec ts;
    gettime(&ts);

    inode.ctime = ts.tv_sec;
    inode.atime = ts.tv_sec;
    inode.mtime = ts.tv_sec;

    if (S_ISDIR(mode)) {   /* Initalize directory structure */
        inode.nlink = 2;
        inode.size = desc->bs;
        
        char *buf = kmalloc(desc->bs, &M_BUFFER, 0);
        struct ext2_dentry *d = (struct ext2_dentry *) buf;
        d->ino = ino;
        d->size = 12;
        d->length = 1;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, ".", 1);
        d = (struct ext2_dentry *) ((char *) d + 12);
        d->ino = dir->ino;
        d->size = desc->bs - 12;
        d->length = 2;
        d->type = EXT2_DENTRY_TYPE_DIR;
        memcpy(d->name, "..", 2);
        ext2_inode_block_write(desc, &inode, ino, 0, buf);
        kfree(buf);
    } else {
        inode.nlink = 1;
        ext2_inode_write(desc, ino, &inode);
    }

    ext2_dentry_create(dir, fn, ino, inode.mode);

    if (ref)
        ext2_inode_build(desc, ino, ref);

    return 0;
}

int ext2_finddir(struct vnode *dir, const char *fn, struct dirent *dirent)
{
    //printk("ext2_vfind(dir=%p, fn=%s, child=%p)\n", dir, fn, child);

    int err = 0;

    struct ext2 *desc = dir->p;

    uint32_t inode = dir->ino;

    struct ext2_inode i;
    if ((err = ext2_inode_read(desc, inode, &i))) {
        /* TODO Error checking */
    }

    uint32_t inode_nr = ext2_dentry_find(desc, &i, fn);

    if (!inode_nr)  /* Not found */
        return -ENOENT;

    inode = inode_nr;

    if (dirent) {
        dirent->d_ino  = inode_nr;
    }

    return 0;
}

int ext2_vget(struct vnode *super, ino_t ino, struct vnode **ref)
{
    struct ext2 *desc = super->p;

    if (ref) {
        ext2_inode_build(desc, ino, ref);
    }

    return 0;
}
