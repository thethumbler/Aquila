#include <core/system.h>
#include <fs/vfs.h>

int vfs_vmknod(struct vnode *dir, const char *name, mode_t mode, dev_t dev, struct uio *uio, struct vnode **ref)
{
    /* Invalid request */
    if (!dir || !dir->fs)
        return -EINVAL;

    /* Not a directory */
    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->fs->vops.vmknod)
        return -ENOSYS;

    int ret = dir->fs->vops.vmknod(dir, name, mode, dev, uio, ref);

    if (!ret && ref && *ref)
        (*ref)->ref++;

    return ret;
}

int vfs_vcreat(struct vnode *dir, const char *name, struct uio *uio, struct vnode **ref)
{
    return vfs_vmknod(dir, name, S_IFREG, 0, uio, ref);
}

int vfs_vmkdir(struct vnode *dir, const char *name, struct uio *uio, struct vnode **ref)
{
    return vfs_vmknod(dir, name, S_IFDIR, 0, uio, ref);
}

int vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    /* Invalid request */
    if (!dir|| !dir->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!dir->fs->vops.vunlink)
        return -ENOSYS;

    return dir->fs->vops.vunlink(dir, fn, uio);
}

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
