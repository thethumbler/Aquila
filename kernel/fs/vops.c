/** \file
 * The vnode operations that work on a directory and a name, or on a
 * vnode's inode: each checks its arguments, then calls the filesystem's
 * operation (`vops`).
 */

#include <core/system.h>
#include <fs/vfs.h>

/**
 * Makes a node in a directory. On success the new vnode has a reference
 * for the caller.
 *
 * @param dir   the directory
 * @param name  the new node's name
 * @param mode  its type (`S_IF*`) and permissions
 * @param dev   its device number, for a device node
 * @param uio   who is asking
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, `-ENOTDIR`, `-ENOSYS` if the filesystem can't, or its error
 */
int vfs_vmknod(struct vnode *dir, const char *name, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    /* invalid request */
    if (!dir || !dir->fs)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    /* the filesystem doesn't support it */
    if (!dir->fs->vops.vmknod)
        return -ENOSYS;

    int ret = dir->fs->vops.vmknod(dir, name, mode, dev, uio, ref);

    if (!ret && ref && *ref)
        (*ref)->ref++;

    return ret;
}

/**
 * Makes a regular file in a directory, with no permissions set.
 *
 * @param dir   the directory
 * @param name  the new file's name
 * @param uio   who is asking
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, or a negative errno
 */
int vfs_vcreat(struct vnode *dir, const char *name, struct uio *uio, struct vnode **ref)
{
    return vfs_vmknod(dir, name, S_IFREG, 0, uio, ref);
}

/**
 * Makes a directory in a directory, with no permissions set.
 *
 * @param dir   the directory
 * @param name  the new directory's name
 * @param uio   who is asking
 * @param ref   if not NULL, set to the new vnode (the caller closes it)
 * @return 0, or a negative errno
 */
int vfs_vmkdir(struct vnode *dir, const char *name, struct uio *uio, struct vnode **ref)
{
    return vfs_vmknod(dir, name, S_IFDIR, 0, uio, ref);
}

/**
 * Removes a name from a directory.
 *
 * @param dir  the directory
 * @param fn   the name
 * @param uio  who is asking
 * @return 0, `-ENOSYS` if the filesystem can't, or its error
 */
int vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    /* invalid request */
    if (!dir|| !dir->fs)
        return -EINVAL;

    /* the filesystem doesn't support it */
    if (!dir->fs->vops.vunlink)
        return -ENOSYS;

    return dir->fs->vops.vunlink(dir, fn, uio);
}

/**
 * Adds a name for an existing file (a hard link) to a directory of the
 * same filesystem.
 *
 * @param dir    the directory
 * @param fn     the new name
 * @param vnode  the file
 * @param uio    who is asking
 * @return 0, `-EXDEV` across filesystems, `-ENOTDIR`, `-ENOSYS`, or the
 *         filesystem's error
 */
int vfs_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio)
{
    if (!dir || !dir->fs || !vnode)
        return -EINVAL;

    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    if (vnode->fs != dir->fs)
        return -EXDEV;

    if (!dir->fs->vops.vlink)
        return -ENOSYS;

    return dir->fs->vops.vlink(dir, fn, vnode, uio);
}

/**
 * Renames an entry, possibly to another directory of the same
 * filesystem.
 *
 * @param odir  the directory it's in
 * @param ofn   its name
 * @param ndir  the directory it goes to
 * @param nfn   its new name
 * @param uio   who is asking
 * @return 0, `-EXDEV` across filesystems, `-ENOTDIR`, `-ENOSYS`, or the
 *         filesystem's error
 */
int vfs_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio)
{
    if (!odir || !odir->fs || !ndir)
        return -EINVAL;

    if (!S_ISDIR(odir->mode) || !S_ISDIR(ndir->mode))
        return -ENOTDIR;

    if (odir->fs != ndir->fs)
        return -EXDEV;

    if (!odir->fs->vops.vrename)
        return -ENOSYS;

    return odir->fs->vops.vrename(odir, ofn, ndir, nfn, uio);
}

/**
 * Has the filesystem store a vnode's attributes (mode, owner, times)
 * after they were changed in memory.
 *
 * @param vnode  the vnode
 * @return 0, `-ENOSYS` if the filesystem can't, or its error
 */
int vfs_setattr(struct vnode *vnode)
{
    if (!vnode || !vnode->fs)
        return -EINVAL;

    if (!vnode->fs->vops.setattr)
        return -ENOSYS;

    return vnode->fs->vops.setattr(vnode);
}

/**
 * Gets the vnode of an inode, by its number. On success it has a
 * reference for the caller.
 *
 * @param super  any vnode of the filesystem (it names the filesystem)
 * @param ino    the inode number
 * @param ref    if not NULL, set to the vnode (the caller closes it)
 * @return 0, `-ENOSYS` if the filesystem can't, or its error
 */
int vfs_vget(struct vnode *super, ino_t ino, struct vnode **ref)
{
    int err = 0;
    struct vnode *vnode = NULL;

    if (!super || !super->fs)
        return -EINVAL;

    if (!super->fs->vops.vget)
        return -ENOSYS;

    if ((err = super->fs->vops.vget(super, ino, &vnode)))
        return err;

    if (vnode) vnode->ref++;
    if (ref) *ref = vnode;

    return err;
}

/**
 * Maps a file into an address space, for `mmap()`: a device's own
 * mapping, or the filesystem's.
 *
 * @param vm_space  the address space
 * @param vm_entry  the mapping, whose object is the file
 * @return 0, `-EINVAL` if the mapping isn't of a file, `-ENOSYS`, or the
 *         filesystem's error
 */
int vfs_map(struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    if (!vm_entry || !vm_entry->vm_object || vm_entry->vm_object->type != VMOBJ_FILE)
        return -EINVAL;

    struct vnode *vnode = (struct vnode *) vm_entry->vm_object->p;

    if (!vnode || !vnode->fs)
        return -EINVAL;

    if (ISDEV(vnode))
        return kdev_map(&VNODE_DEV(vnode), vm_space, vm_entry);

    if (!vnode->fs->vops.map)
        return -ENOSYS;

    return vnode->fs->vops.map(vm_space, vm_entry);
}
