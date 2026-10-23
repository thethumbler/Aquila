#include <ext2.h>
#include <core/time.h>
#include <fs/iobuf.h>

/*
 * A file's data moves a block at a time through a kernel buffer: the
 * block's piece of the iobuf is copied in or out of it.
 */

ssize_t ext2_read(struct vnode *node, off_t offset, struct iobuf *io)
{
    int err = 0;

    struct ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, node->ino, &inode)))
        return err;

    if ((size_t) offset >= inode.size)
        return 0;

    size_t size = MIN(io->resid, (size_t) (inode.size - offset));

    /* a fast symbolic link keeps its target in the block pointers */
    if (S_ISLNK(inode.mode) && !inode.blocks)
        return iobuf_out(io, (char *) inode.direct_pointer + offset, size);
    
    char *block = kmalloc(bs, &M_BUFFER, 0);

    if (!block)
        return -ENOMEM;

    size_t done = 0;
    ssize_t r = 0;

    while (done < size) {
        size_t off = (offset + done) % bs;
        size_t len = MIN(bs - off, size - done);

        if ((r = ext2_inode_block_read(desc, &inode, (offset + done) / bs, block)))
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

ssize_t ext2_write(struct vnode *node, off_t offset, struct iobuf *io)
{
    size_t size = io->resid;

    vfs_log(LOG_DEBUG, "ext2_write(inode=%p, offset=%d, size=%d)\n", node, offset, size);

    int err = 0;
    struct ext2 *desc = node->p;

    size_t bs = desc->bs;
    struct ext2_inode ext2_inode;
    
    if ((err = ext2_inode_read(desc, node->ino, &ext2_inode)))
        return err;

    /* a short symbolic link keeps its target in the block pointers (a
     * "fast" link, e2fsck expects those under 60 bytes) */
    if (S_ISLNK(node->mode) && !ext2_inode.blocks &&
        (size_t) offset + size < sizeof(ext2_inode.direct_pointer) + 12) {
        ssize_t r = iobuf_in(io, (char *) ext2_inode.direct_pointer + offset, size);

        if (r <= 0)
            return r;

        if ((size_t) offset + r > ext2_inode.size)
            ext2_inode.size = node->size = (size_t) offset + r;

        return ext2_inode_write(desc, node->ino, &ext2_inode) < 0 ? -EIO : r;
    }

    size_t old_size = ext2_inode.size;

    if ((size_t) offset + size > ext2_inode.size) {
        ext2_inode.size = (size_t) offset + size;
        ext2_trunc(node, (size_t) offset + size);
    }

    size = MIN(size, (size_t) (ext2_inode.size - offset));
    
    char *block = kmalloc(bs, &M_BUFFER, 0);

    if (!block)
        return -ENOMEM;

    size_t done = 0;
    ssize_t r = 0;

    while (done < size) {
        size_t off = (offset + done) % bs;
        size_t len = MIN(bs - off, size - done);

        /* part of a block: the rest of it stays (if it can't be read, it
         * isn't written over) */
        if ((off || len < bs) &&
            (r = ext2_inode_block_read(desc, &ext2_inode, (offset + done) / bs, block)))
            break;

        if ((r = iobuf_in(io, block + off, len)) <= 0)
            break;

        if ((err = ext2_inode_block_write(desc, &ext2_inode, node->ino, (offset + done) / bs, block))) {
            r = err;
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
        ext2_trunc(node, MAX(old_size, (size_t) offset + done));

    /* report a short write if some data made it to disk */
    return done ? (ssize_t) done : r;
}

ssize_t ext2_readdir(struct vnode *dir, off_t offset, struct dirent *dirent)
{
    int err = 0;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct ext2 *desc = dir->p;
    struct ext2_inode inode;
    
    if ((err = ext2_inode_read(desc, dir->ino, &inode)))
        return err;

    if (!S_ISDIR(inode.mode))
        return -ENOTDIR;

    size_t bs = desc->bs;
    size_t blocks_nr = inode.size / bs;

    /* not on the stack: a block is up to 4 KiB, i386's stack is 8 */
    char *buf = kmalloc(bs, &M_BUFFER, 0);
    if (!buf) return -ENOMEM;

    struct ext2_dentry *dentry;
    off_t idx = 0;
    char found = 0;

    for (size_t i = 0; i < blocks_nr; ++i) {
        if ((err = ext2_inode_block_read(desc, &inode, i, buf))) {
            kfree(buf);
            return err;
        }

        dentry = (struct ext2_dentry *) buf;

        /* an entry, its header and its name, in the block */
        while ((char *) dentry + sizeof(*dentry) <= buf + bs) {
            if (!dentry->size)  /* corrupted entry */
                break;

            if ((char *) dentry->name + dentry->length > buf + bs)
                break;

            /* unused entry */
            if (!dentry->ino) {
                dentry = (struct ext2_dentry *) ((char *) dentry + dentry->size);
                continue;
            }

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

        if (found)
            break;
    }

    kfree(buf);
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

    /* otherwise free the blocks past the new end */
    struct ext2 *desc = inode->p;
    struct ext2_inode einode;
    size_t bs = desc->bs;
    int err;

    if ((err = ext2_inode_read(desc, inode->ino, &einode)))
        return err;

    ext2_blocks_free(desc, &einode, (len + bs - 1) / bs);

    /* the rest of the last block reads as zeros if the file grows again
     * (a block that can't be read isn't written over) */
    if (len % bs) {
        char *buf = kmalloc(bs, &M_BUFFER, 0);

        if (!buf)
            return -ENOMEM;

        if (!(err = ext2_inode_block_read(desc, &einode, len / bs, buf))) {
            memset(buf + len % bs, 0, bs - len % bs);
            err = ext2_inode_block_write(desc, &einode, inode->ino, len / bs, buf);
        }

        kfree(buf);

        if (err)
            return err;
    }

    einode.size = len;
    inode->size = len;
    return ext2_inode_write(desc, inode->ino, &einode) < 0 ? -EIO : 0;
}

int ext2_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int err = 0;

    if (!fn || !*fn)
        return -EINVAL;

    struct ext2 *desc = dir->p;
    struct ext2_inode dir_inode;
    
    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode)))
        return err;

    /* file exists */
    ino_t ino;

    if (!(err = ext2_dentry_find(desc, &dir_inode, fn, &ino)))
        return -EEXIST;

    if (err != -ENOENT)
        return err;

    if ((err = ext2_inode_alloc(desc, &ino)))
        return err;

    struct ext2_inode inode;

    memset(&inode, 0, sizeof(struct ext2_inode));

    inode.mode = mode;
    inode.uid  = uio->uid;
    inode.gid  = uio->gid;

    /* a device node's number, where ext2_inode_build() reads it */
    if (S_ISCHR(mode) || S_ISBLK(mode))
        inode.direct_pointer[0] = DEV(DEV_MAJOR(dev), DEV_MINOR(dev));

    struct timespec ts;
    gettime(&ts);

    inode.ctime = ts.tv_sec;
    inode.atime = ts.tv_sec;
    inode.mtime = ts.tv_sec;

    if (S_ISDIR(mode)) {   /* Initalize directory structure */
        inode.nlink = 2;
        inode.size = desc->bs;
        
        /* zeroed: the names are padded with NULs (no memory: the inode
         * goes again, below) */
        char *buf = kmalloc(desc->bs, &M_BUFFER, M_ZERO);

        if (!buf) {
            err = -ENOMEM;
        } else {
            struct ext2_dentry *d = (struct ext2_dentry *) buf;
            d->ino = ino;
            d->size = 12;
            d->length = 1;
            d->type = desc->filetype ? EXT2_DENTRY_TYPE_DIR : 0;
            memcpy(d->name, ".", 1);
            d = (struct ext2_dentry *) ((char *) d + 12);
            d->ino = dir->ino;
            d->size = desc->bs - 12;
            d->length = 2;
            d->type = desc->filetype ? EXT2_DENTRY_TYPE_DIR : 0;
            memcpy(d->name, "..", 2);
            err = ext2_inode_block_write(desc, &inode, ino, 0, buf);
            kfree(buf);
        }
    } else {
        inode.nlink = 1;
        err = ext2_inode_write(desc, ino, &inode);
    }

    /* not in the directory: it goes again */
    if (err || (err = ext2_dentry_create(dir, fn, ino, inode.mode))) {
        ext2_inode_discard(desc, ino);
        return err;
    }

    if (S_ISDIR(mode)) {
        /* the new directory's .. */
        dir->nlink++;
        ext2_inode_sync(dir);

        desc->groups[(ino - 1) / desc->superblock->ginodes].directories_count++;
        ext2_bgd_table_rewrite(desc);
    }

    return ref ? ext2_inode_build(desc, ino, ref) : 0;
}

int ext2_finddir(struct vnode *dir, const char *fn, struct dirent *dirent)
{
    int err = 0;

    struct ext2 *desc = dir->p;

    uint32_t inode = dir->ino;

    struct ext2_inode i;
    ino_t inode_nr;

    if ((err = ext2_inode_read(desc, inode, &i)) ||
        (err = ext2_dentry_find(desc, &i, fn, &inode_nr)))
        return err;

    if (dirent)
        dirent->d_ino = inode_nr;

    return 0;
}

int ext2_vget(struct vnode *super, ino_t ino, struct vnode **ref)
{
    struct ext2 *desc = super->p;

    return ref ? ext2_inode_build(desc, ino, ref) : 0;
}

int ext2_setattr(struct vnode *vnode)
{
    struct ext2 *desc = vnode->p;
    struct ext2_inode inode;
    int err;

    if ((err = ext2_inode_read(desc, vnode->ino, &inode)))
        return err;

    inode.mode  = vnode->mode;
    inode.uid   = vnode->uid;
    inode.gid   = vnode->gid;
    inode.atime = vnode->atime.tv_sec;
    inode.mtime = vnode->mtime.tv_sec;
    inode.ctime = vnode->ctime.tv_sec;

    return ext2_inode_write(desc, vnode->ino, &inode);
}
