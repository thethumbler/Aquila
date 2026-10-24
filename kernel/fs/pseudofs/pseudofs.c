/** \file
 * pseudofs: the directories of the filesystems that live in memory
 * (tmpfs, and so devfs and devpts). A directory is a list of its entries,
 * each a name and the entry's vnode, which is also its inode (its inode
 * number is its address).
 */

#include <core/system.h>
#include <core/time.h>

#include <fs/pseudofs.h>

MALLOC_DEFINE(M_PSEUDOFS_DENT, "pseudofs-dirent", "pseudofs directory entry");

/*
 * A directory is the list of its entries (vnode->p). Its ".." is one of
 * them, as on a disk: it points to the parent's vnode (without a
 * reference, since the parent can't go while it has the directory).
 * finddir finds it like any other entry, and rename moves it; readdir
 * skips it, and makes up "." and ".." itself. A filesystem's root has no
 * "..".
 */
static const char dotdot[] = "..";

/** Copies an entry's name into a `struct dirent`, cut to fit. */
static void pseudofs_dirent_name(struct dirent *dirent, const char *name)
{
    size_t len = strlen(name);

    if (len >= MAXNAMELEN)
        len = MAXNAMELEN - 1;

    memcpy(dirent->d_name, name, len);
    dirent->d_name[len] = '\0';
}

/**
 * Makes a node in a directory: a new vnode, owned by the caller, and an
 * entry for it. A new directory gets its `..` entry.
 *
 * @param dir   the directory
 * @param fn    the new node's name
 * @param mode  its type (`S_IF*`) and permissions
 * @param dev   its device number, for a device node
 * @param uio   who is asking (the owner)
 * @param ref   if not NULL, set to the new vnode
 * @return 0, `-EEXIST`, `-ENAMETOOLONG`, or `-ENOMEM`
 */
int pseudofs_vmknod(struct vnode *dir, const char *fn, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    int err = 0;
    struct vnode *vnode = NULL;
    struct pseudofs_dirent *dirent = NULL;

    if (strlen(fn) >= MAXNAMELEN)
        return -ENAMETOOLONG;

    struct dirent dirent_tmp;
    if (!pseudofs_finddir(dir, fn, &dirent_tmp)) {
        err = -EEXIST;
        goto error;
    }

    vnode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!vnode) goto error_nomem;
    
    vnode->ino   = (ino_t) vnode;
    vnode->mode  = mode;
    vnode->size  = 0;
    vnode->uid   = uio->uid;
    vnode->gid   = uio->gid;
    vnode->nlink = S_ISDIR(mode)? 2 : 1;
    vnode->rdev  = dev;

    struct timespec ts;
    gettime(&ts);

    vnode->ctime = ts;
    vnode->atime = ts;
    vnode->mtime = ts;

    vnode->fs = dir->fs;    /* the directory's filesystem */

    struct pseudofs_dirent *cur_dir = (struct pseudofs_dirent *) dir->p;

    dirent = kmalloc(sizeof(struct pseudofs_dirent), &M_PSEUDOFS_DENT, 0);
    if (!dirent) goto error_nomem;

    dirent->d_ino  = vnode;
    dirent->d_name = strdup(fn);

    if (!dirent->d_name) goto error_nomem;

    /* the new directory's .. */
    if (S_ISDIR(mode)) {
        struct pseudofs_dirent *parent = kmalloc(sizeof(struct pseudofs_dirent), &M_PSEUDOFS_DENT, 0);
        if (!parent) goto error_nomem;

        parent->d_name = dotdot;
        parent->d_ino  = dir;
        parent->next   = NULL;
        vnode->p       = parent;
        dir->nlink++;
    }

    dirent->next  = cur_dir;
    dir->p        = dirent;

    if (ref)
        *ref = vnode;

    return 0;

error_nomem:
    err = -ENOMEM;
    goto error;

error:
    if (vnode)
        kfree(vnode);

    if (dirent) {
        if (dirent->d_name)
            kfree((void *) dirent->d_name);

        kfree(dirent);
    }

    return err;
}

/** Finds the entry `fn` of directory `dir`, and the entry before it in
 * `*prev` (NULL if it's the first). Returns NULL if there's none. */
static struct pseudofs_dirent *entry_find(struct vnode *dir, const char *fn, struct pseudofs_dirent **prev)
{
    *prev = NULL;

    for (struct pseudofs_dirent *e = dir->p; e; e = e->next) {
        if (!strcmp(e->d_name, fn))
            return e;

        *prev = e;
    }

    return NULL;
}

/** Removes `entry` (which comes after `prev`) from directory `dir`, and
 * drops a link to its vnode, which is freed if nothing references it. */
static void entry_remove(struct vnode *dir, struct pseudofs_dirent *entry, struct pseudofs_dirent *prev)
{
    struct vnode *vnode = entry->d_ino;

    if (prev)
        prev->next = entry->next;
    else
        dir->p = entry->next;

    kfree((void *) entry->d_name);
    kfree(entry);

    if (S_ISDIR(vnode->mode)) {
        /* a directory loses its . link, and the parent the link of its
         * .. */
        vnode->nlink = 0;
        dir->nlink--;
    } else {
        vnode->nlink--;
    }

    /* free it if nothing references it anymore */
    if (!vnode->nlink && !vnode->ref) {
        vnode->ref = 1; /* vfs_close() drops this one */
        vfs_close(vnode);
    }
}

/**
 * Removes an entry from a directory.
 *
 * @param vnode  the directory
 * @param fn     the entry's name
 * @param uio    who is asking
 * @return 0, `-ENOTDIR`, or `-ENOENT`
 */
int pseudofs_vunlink(struct vnode *vnode, const char *fn, struct uio *uio)
{
    struct pseudofs_dirent *prev;

    if (!S_ISDIR(vnode->mode))
        return -ENOTDIR;

    struct pseudofs_dirent *entry = entry_find(vnode, fn, &prev);

    if (!entry)
        return -ENOENT;

    entry_remove(vnode, entry, prev);
    return 0;
}

/**
 * Adds an entry for an existing vnode (a hard link) to a directory.
 *
 * @param dir    the directory
 * @param fn     the new name
 * @param vnode  the vnode
 * @param uio    who is asking
 * @return 0, `-EEXIST`, `-ENAMETOOLONG`, or `-ENOMEM`
 */
int pseudofs_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio)
{
    struct pseudofs_dirent *prev;

    if (strlen(fn) >= MAXNAMELEN)
        return -ENAMETOOLONG;

    if (entry_find(dir, fn, &prev))
        return -EEXIST;

    struct pseudofs_dirent *entry = kmalloc(sizeof(struct pseudofs_dirent), &M_PSEUDOFS_DENT, 0);
    if (!entry)
        return -ENOMEM;

    if (!(entry->d_name = strdup(fn))) {
        kfree(entry);
        return -ENOMEM;
    }

    entry->d_ino = vnode;
    entry->next  = dir->p;
    dir->p       = entry;

    vnode->nlink++;
    return 0;
}

/**
 * Renames an entry, possibly to another directory, replacing an entry at
 * the new name. The VFS has checked the replaced entry already (its type,
 * that a directory is empty).
 *
 * @param odir  the directory the entry is in
 * @param ofn   its name
 * @param ndir  the directory it goes to
 * @param nfn   its new name
 * @param uio   who is asking
 * @return 0, `-ENOENT`, `-ENAMETOOLONG`, or `-ENOMEM`
 */
int pseudofs_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio)
{
    struct pseudofs_dirent *oprev, *nprev;

    if (strlen(nfn) >= MAXNAMELEN)
        return -ENAMETOOLONG;

    struct pseudofs_dirent *entry = entry_find(odir, ofn, &oprev);
    if (!entry)
        return -ENOENT;

    char *name = strdup(nfn);
    if (!name)
        return -ENOMEM;

    /* replace the entry at the new name, if there's one */
    struct pseudofs_dirent *old = entry_find(ndir, nfn, &nprev);
    if (old) {
        entry_remove(ndir, old, nprev);

        /* removing it may have removed the entry before ours: find it
         * again */
        entry_find(odir, ofn, &oprev);
    }

    if (oprev)
        oprev->next = entry->next;
    else
        odir->p = entry->next;

    kfree((void *) entry->d_name);
    entry->d_name = name;
    entry->next   = ndir->p;
    ndir->p       = entry;

    /* a directory's .. now points to its new parent */
    if (S_ISDIR(entry->d_ino->mode)) {
        struct pseudofs_dirent *prev, *up = entry_find(entry->d_ino, dotdot, &prev);

        if (up)
            up->d_ino = ndir;

        odir->nlink--;
        ndir->nlink++;
    }

    return 0;
}

/**
 * Stores a vnode's attributes: there's nothing to do, since they only
 * live in the vnode.
 *
 * @param vnode  the vnode
 * @return 0
 */
int pseudofs_setattr(struct vnode *vnode)
{
    return 0;
}

/**
 * Reads a directory's entry at an offset. The entries are `.` and `..`
 * first, then the list's entries (except its own `..`).
 *
 * @param dir     the directory
 * @param offset  which entry
 * @param dirent  the entry, filled in
 * @return 1 if an entry was read, or 0 past the last one
 */
ssize_t pseudofs_readdir(struct vnode *dir, off_t offset, struct dirent *dirent)
{
    if (offset == 0) {
        strcpy(dirent->d_name, ".");
        return 1;
    }

    if (offset == 1) {
        strcpy(dirent->d_name, "..");
        return 1;
    }

    struct pseudofs_dirent *_dirent = (struct pseudofs_dirent *) dir->p;

    if (!_dirent)
        return 0;

    int found = 0;

    offset -= 2;

    int i = 0;
    for (struct pseudofs_dirent *e = _dirent; e; e = e->next) {
        /* .. was made up above */
        if (e->d_name == dotdot)
            continue;

        if (i == offset) {
            found = 1;
            dirent->d_ino = e->d_ino->ino;
            pseudofs_dirent_name(dirent, e->d_name);
            break;
        }
        ++i;
    }

    return found;
}

/**
 * Finds the entry of a directory that has a name.
 *
 * @param dir     the directory
 * @param name    the name
 * @param dirent  the entry, filled in
 * @return 0, `-ENOENT`, `-ENOTDIR`, or `-EINVAL`
 */
int pseudofs_finddir(struct vnode *dir, const char *name, struct dirent *dirent)
{
    if (!dir || !name || !dirent)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    struct pseudofs_dirent *_dirent = (struct pseudofs_dirent *) dir->p;
    if (!_dirent) return -ENOENT;

    for (struct pseudofs_dirent *e = _dirent; e; e = e->next) {
        if (!strcmp(name, e->d_name)) {
            dirent->d_ino = e->d_ino->ino;
            pseudofs_dirent_name(dirent, e->d_name);
            return 0;
        }
    }

    return -ENOENT;
}

/**
 * Frees a vnode that has no names or references left, and its cached
 * pages. Its names belong to the directory entries, which were freed when
 * they were removed.
 *
 * @param vnode  the vnode
 * @return 0
 */
int pseudofs_close(struct vnode *vnode)
{
    vnode_pages_free(vnode);
    kfree(vnode);
    return 0;
}
