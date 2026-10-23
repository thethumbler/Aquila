/** \file
 * Removing, linking and renaming entries, and freeing what a file used.
 *
 * Unlinking drops the link count, the inode and its blocks are freed when
 * the vnode is closed for the last time (ext2_close), so a file that is
 * still open stays readable.
 */

#include <ext2.h>
#include <core/time.h>
#include <ds/bitmap.h>
#include <fs/vcache.h>

/** free the blocks of the tree at `block' (a data block at depth 0, else
 * an indirect block `depth' levels above the data), from data block
 * `from' of the tree on; returns the block, 0 if it was freed */
static uint32_t free_tree(struct ext2 *desc, struct ext2_inode *inode, uint32_t block, int depth, size_t from)
{
    size_t bs = desc->bs, p = bs / 4, span = 1;

    if (!block)
        return 0;

    if (depth) {
        uint32_t *table = kmalloc(bs, &M_BUFFER, 0);

        for (int i = 1; i < depth; ++i)
            span *= p;

        /* a table that can't be read stays, what's under it with it (its
         * pointers aren't known: nothing is freed for them) */
        if (!table || ext2_block_read(desc, block, table)) {
            kfree(table);
            return block;
        }

        for (size_t i = from / span; i < p; ++i)
            table[i] = free_tree(desc, inode, table[i], depth - 1, i == from / span ? from % span : 0);

        /* part of the tree stays */
        if (from) {
            ext2_block_write(desc, block, table);
            kfree(table);
            return block;
        }

        kfree(table);
    }

    if (ext2_block_free(desc, block))
        return block;

    inode->blocks -= bs / 512;
    return 0;
}

/** free the blocks of `inode' from data block `from' on */
void ext2_blocks_free(struct ext2 *desc, struct ext2_inode *inode, size_t from)
{
    size_t p = desc->bs / 4, base = EXT2_DIRECT_POINTERS;

    /* a fast symbolic link keeps its target in the block pointers */
    if (S_ISLNK(inode->mode) && !inode->blocks)
        return;

    for (size_t i = from; i < EXT2_DIRECT_POINTERS; ++i)
        inode->direct_pointer[i] = free_tree(desc, inode, inode->direct_pointer[i], 0, 0);

    inode->singly_indirect_pointer =
        free_tree(desc, inode, inode->singly_indirect_pointer, 1, from > base ? from - base : 0);
    base += p;
    inode->doubly_indirect_pointer =
        free_tree(desc, inode, inode->doubly_indirect_pointer, 2, from > base ? from - base : 0);
    base += p * p;
    inode->triply_indirect_pointer =
        free_tree(desc, inode, inode->triply_indirect_pointer, 3, from > base ? from - base : 0);
}

/** mark inode `ino' free (a bitmap that can't be read isn't written back:
 * it stays used) */
static void ext2_inode_free(struct ext2 *desc, ino_t ino, int dir)
{
    uint32_t group = (ino - 1) / desc->superblock->ginodes;
    uint32_t index = (ino - 1) % desc->superblock->ginodes;
    uint32_t *buf = kmalloc(desc->bs, &M_BUFFER, 0);
    struct bitmap bm = {.map = buf, .max_idx = desc->superblock->ginodes};

    if (!buf)
        return;

    if (ext2_block_read(desc, desc->groups[group].imap, buf)) {
        kfree(buf);
        return;
    }

    bitmap_clear(&bm, index);
    int err = ext2_block_write(desc, desc->groups[group].imap, buf);
    kfree(buf);

    if (err)
        return;

    desc->groups[group].free_inodes++;
    if (dir)
        desc->groups[group].directories_count--;
    ext2_bgd_table_rewrite(desc);

    desc->superblock->free_inodes++;
    ext2_superblock_rewrite(desc);
}

/** free inode `ino' and everything it uses, it has no links left */
void ext2_inode_discard(struct ext2 *desc, ino_t ino)
{
    struct ext2_inode inode;
    struct timespec ts;

    if (ext2_inode_read(desc, ino, &inode))
        return;

    ext2_blocks_free(desc, &inode, 0);

    gettime(&ts);
    inode.nlink = 0;
    inode.size  = 0;
    inode.dtime = ts.tv_sec;

    /* written as freed, or it stays used */
    if (!ext2_inode_write(desc, ino, &inode))
        ext2_inode_free(desc, ino, S_ISDIR(inode.mode));
}

/** the last reference to a vnode is gone */
int ext2_close(struct vnode *vnode)
{
    struct ext2 *desc = vnode->p;

    /* unlinked while it was open */
    if (!vnode->nlink) {
        ext2_inode_discard(desc, vnode->ino);
        vcache_remove(desc->vcache, vnode);
        kfree(vnode);
    }

    return 0;
}

/* call fn on the entry `name' of directory `dir' and write the block back,
 * prev is the entry before it in its block (NULL if first); the entry's
 * inode into `*ino': 0, -ENOENT, or an error */
static int dentry_update(struct vnode *dir, const char *name,
                         void (*fn)(struct ext2_dentry *d, struct ext2_dentry *prev, void *arg),
                         void *arg, ino_t *ino)
{
    struct ext2 *desc = dir->p;
    struct ext2_inode dir_inode;
    size_t bs = desc->bs, len = strlen(name);
    int err;

    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode)))
        return err;

    char *buf = kmalloc(bs, &M_BUFFER, 0);

    if (!buf)
        return -ENOMEM;

    err = -ENOENT;

    for (size_t i = 0; i < dir_inode.size / bs && err == -ENOENT; ++i) {
        struct ext2_dentry *d = (struct ext2_dentry *) buf, *prev = NULL;
        int r;

        if ((r = ext2_inode_block_read(desc, &dir_inode, i, buf))) {
            err = r;
            break;
        }

        while ((char *) d < buf + bs && d->size) {
            if (d->ino && d->length == len && !memcmp(d->name, name, len)) {
                *ino = d->ino;
                fn(d, prev, arg);
                err = ext2_inode_block_write(desc, &dir_inode, dir->ino, i, buf);
                break;
            }

            prev = d;
            d = (struct ext2_dentry *) ((char *) d + d->size);
        }
    }

    kfree(buf);
    return err;
}

static void dentry_remove(struct ext2_dentry *d, struct ext2_dentry *prev, void *arg)
{
    /* the previous entry takes its space, the first one of a block is
     * marked unused */
    if (prev)
        prev->size += d->size;
    else
        d->ino = 0;
}

static void dentry_set_ino(struct ext2_dentry *d, struct ext2_dentry *prev, void *arg)
{
    d->ino = *(ino_t *) arg;
}

int ext2_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    struct ext2 *desc = dir->p;
    struct vnode *vnode = NULL;
    struct timespec ts;
    ino_t ino = 0;
    int err;

    if ((err = dentry_update(dir, fn, dentry_remove, NULL, &ino)))
        return err;

    if ((err = ext2_inode_build(desc, ino, &vnode)))
        return err;

    gettime(&ts);

    if (S_ISDIR(vnode->mode)) {
        /* its . and the parent's link through its .. */
        vnode->nlink = 0;
        dir->nlink--;
    } else {
        vnode->nlink--;
    }

    dir->mtime = dir->ctime = ts;
    ext2_inode_sync(dir);

    vnode->ctime = ts;
    ext2_inode_sync(vnode);

    /* nothing has it open */
    if (!vnode->ref) {
        vnode->ref = 1;
        vfs_close(vnode);
    }

    return 0;
}

int ext2_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio)
{
    struct ext2 *desc = dir->p;
    struct ext2_inode dir_inode;
    struct timespec ts;
    int err;

    if ((err = ext2_inode_read(desc, dir->ino, &dir_inode)))
        return err;

    ino_t ino;

    if (!(err = ext2_dentry_find(desc, &dir_inode, fn, &ino)))
        return -EEXIST;

    if (err != -ENOENT)
        return err;

    if ((err = ext2_dentry_create(dir, fn, vnode->ino, vnode->mode)))
        return err;

    gettime(&ts);
    vnode->nlink++;
    vnode->ctime = ts;
    return ext2_inode_sync(vnode);
}

int ext2_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio)
{
    struct ext2 *desc = odir->p;
    struct ext2_inode inode;
    struct vnode *vnode = NULL;
    struct timespec ts;
    int err;

    if ((err = ext2_inode_read(desc, odir->ino, &inode)))
        return err;

    ino_t ino, other, dummy;

    if ((err = ext2_dentry_find(desc, &inode, ofn, &ino)))
        return err;

    if ((err = ext2_inode_build(desc, ino, &vnode)))
        return err;

    /* the entry it replaces (the vfs checked its type) */
    if ((err = ext2_inode_read(desc, ndir->ino, &inode)))
        return err;

    if (!(err = ext2_dentry_find(desc, &inode, nfn, &other)))
        err = ext2_vunlink(ndir, nfn, uio);

    if (err && err != -ENOENT)
        return err;

    if ((err = ext2_dentry_create(ndir, nfn, ino, vnode->mode)))
        return err;

    if ((err = dentry_update(odir, ofn, dentry_remove, NULL, &dummy)))
        return err;

    gettime(&ts);

    /* a directory's .. now links to the new parent */
    if (S_ISDIR(vnode->mode) && odir != ndir) {
        ino_t parent = ndir->ino;

        if ((err = dentry_update(vnode, "..", dentry_set_ino, &parent, &dummy)))
            return err;

        odir->nlink--;
        ndir->nlink++;
    }

    odir->mtime = odir->ctime = ts;
    ndir->mtime = ndir->ctime = ts;
    vnode->ctime = ts;

    ext2_inode_sync(odir);
    if (ndir != odir)
        ext2_inode_sync(ndir);

    return ext2_inode_sync(vnode);
}
