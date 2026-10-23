#include <core/system.h>
#include <core/time.h>

#include <fs/pseudofs.h>

MALLOC_DEFINE(M_PSEUDOFS_DENT, "pseudofs-dirent", "pseudofs directory entry");

/*
 * A directory is a list of its entries (vnode->p). Its ".." is one of
 * them, as on a disk: the parent's vnode (not a reference, the parent
 * can't go while it has the directory), which finddir finds as any other
 * and rename moves; readdir makes up "." and ".." itself, it skips it. A
 * filesystem's root has none: it's a root.
 */
static const char dotdot[] = "..";

/** bounded copy of an entry name into a struct dirent */
static void pseudofs_dirent_name(struct dirent *dirent, const char *name)
{
    size_t len = strlen(name);

    if (len >= MAXNAMELEN)
        len = MAXNAMELEN - 1;

    memcpy(dirent->d_name, name, len);
    dirent->d_name[len] = '\0';
}

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

    vnode->fs = dir->fs;    /* Copy filesystem from directory */

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

/** the entry `fn' of directory `dir', and the entry before it (NULL if first) */
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

/** remove `entry' (after `prev') from directory `dir' and drop a link to its vnode */
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
        /* its . and the parent's link through its .. */
        vnode->nlink = 0;
        dir->nlink--;
    } else {
        vnode->nlink--;
    }

    /* free it if nothing references it anymore */
    if (!vnode->nlink && !vnode->ref) {
        vnode->ref = 1; /* vfs_close will decrement ref */
        vfs_close(vnode);
    }
}

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

/** the checks of the new entry (type, empty directory) are done by the vfs */
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

    /* replace an existing entry */
    struct pseudofs_dirent *old = entry_find(ndir, nfn, &nprev);
    if (old) {
        entry_remove(ndir, old, nprev);

        /* removing it may have unlinked the entry before ours */
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

    /* a directory's .. now links to the new parent */
    if (S_ISDIR(entry->d_ino->mode)) {
        struct pseudofs_dirent *prev, *up = entry_find(entry->d_ino, dotdot, &prev);

        if (up)
            up->d_ino = ndir;

        odir->nlink--;
        ndir->nlink++;
    }

    return 0;
}

/** attributes only live in the vnode */
int pseudofs_setattr(struct vnode *vnode)
{
    return 0;
}

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
        /* made up above */
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

/** the names are the entries', freed when they go (pseudofs_vunlink) */
int pseudofs_close(struct vnode *vnode)
{
    kfree(vnode);
    return 0;
}
