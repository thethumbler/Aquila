/**
 * \defgroup fs-tmpfs kernel/fs/tmpfs
 * \brief temporary filesystem (tmpfs) handler
 */

#include <core/system.h>
#include <core/module.h>
#include <core/string.h>
#include <core/panic.h>
#include <core/time.h>

#include <mm/mm.h>

#include <fs/vfs.h>
#include <fs/pseudofs.h>
#include <fs/tmpfs.h>
#include <fs/posix.h>

#include <bits/errno.h>
#include <bits/dirent.h>

static int tmpfs_vget(struct vnode *super, ino_t ino, struct vnode **vnode)
{
    /* vnode is always present in memory */
    struct vnode *node = (struct vnode *) ino;
    if (vnode) *vnode = node;

    return 0;
}

static int tmpfs_close(struct vnode *vnode)
{
    /* Inode is always present in memory */

    if (!vnode->ref && !vnode->nlink) {
        /* vnode is no longer referenced */
        kfree(vnode->p);
        pseudofs_close(vnode);
    }

    return 0;
}

static ssize_t tmpfs_read(struct vnode *node, off_t offset, size_t size, void *buf)
{
    if (!node->size)
        return 0;

    ssize_t r = MIN(node->size - offset, size);
    memcpy(buf, (char *) node->p + offset, r);
    return r;
}

static ssize_t tmpfs_write(struct vnode *node, off_t offset, size_t size, void *buf)
{
    if (!node->size) {
        size_t sz = (size_t) offset + size;
        node->p = kmalloc(sz, &M_BUFFER, 0);
        node->size = sz;
    }

    if (((size_t) offset + size) > node->size) {    /* Reallocate */
        size_t sz = (size_t) offset + size;
        void *new = kmalloc(sz, &M_BUFFER, 0);
        memcpy(new, node->p, node->size);
        kfree(node->p);
        node->p = new;
        node->size = sz;
    }

    memcpy((char *) node->p + offset, buf, size);
    return size;
}

static int tmpfs_trunc(struct vnode *vnode, off_t len)
{
    if (!vnode)
        return -EINVAL;

    if ((size_t) len == vnode->size)
        return 0;

    if (len == 0) {
        kfree(vnode->p);
        vnode->size = 0;
        return 0;
    }

    size_t sz = MIN((size_t) len, vnode->size);
    char *buf = kmalloc(len, &M_BUFFER, 0);

    if (!buf)
        panic("failed to allocate buffer");

    memcpy(buf, vnode->p, sz);

    if ((size_t) len > vnode->size)
        memset(buf + vnode->size, 0, len - vnode->size);

    kfree(vnode->p);
    vnode->p    = buf;
    vnode->size = len;
    return 0;
}

/* ================ File Operations ================ */

static int tmpfs_file_can_read(struct file *file, size_t size)
{
    if ((size_t) file->offset + size < file->vnode->size)
        return 1;

    return 0;
}

static int tmpfs_file_can_write(struct file *file __unused, size_t size __unused)
{
    /* TODO impose limit */
    return 1;
}

static int tmpfs_file_eof(struct file *file)
{
    return (size_t) file->offset == file->vnode->size;
}

static int tmpfs_init()
{
    return vfs_install(&tmpfs);
}

static int tmpfs_mount(const char *dir, int flags __unused, void *data __unused)
{
    /* Initalize new */
    struct vnode *tmpfs_root = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!tmpfs_root) return -ENOMEM;

    uint32_t mode = 0777;

    struct mdata {
        char *dev;
        char *opt;
    } *mdata = data;

    if (mdata->opt) {
        char **tokens = tokenize(mdata->opt, ',');
        for (char **token_p = tokens; *token_p; ++token_p) {
            char *token = *token_p;
            if (!strncmp(token, "mode=", 5)) {    /* ??? */
                char *t = token + 5;
                mode = 0;
                while (*t) {
                    mode <<= 3;
                    mode |= (*t++ - '0');
                }
            }
        }
    }

    tmpfs_root->ino   = (vino_t) tmpfs_root;
    tmpfs_root->mode  = S_IFDIR | mode;
    tmpfs_root->size  = 0;
    tmpfs_root->nlink = 2;
    tmpfs_root->fs    = &tmpfs;
    tmpfs_root->p     = NULL;
    tmpfs_root->ref   = 1;

    struct timespec ts;
    gettime(&ts);

    tmpfs_root->ctime = ts;
    tmpfs_root->atime = ts;
    tmpfs_root->mtime = ts;

    vfs_bind(dir, tmpfs_root);

    return 0;
}

struct fs tmpfs = {
    .name   = "tmpfs",
    .nodev  = 1,
    .init   = tmpfs_init,
    .mount  = tmpfs_mount,

    .vops = {
        .read    = tmpfs_read,
        .write   = tmpfs_write,
        .close   = tmpfs_close,
        .trunc   = tmpfs_trunc,

        .readdir = pseudofs_readdir,
        .finddir = pseudofs_finddir,

        .vmknod  = pseudofs_vmknod,
        .vunlink = pseudofs_vunlink,
        .vget    = tmpfs_vget,
    },
    
    .fops = {
        .open    = posix_file_open,
        .close   = posix_file_close,
        .read    = posix_file_read,
        .write   = posix_file_write, 
        .ioctl   = posix_file_ioctl,
        .lseek   = posix_file_lseek,
        .readdir = posix_file_readdir,
        .trunc   = posix_file_trunc,

        .can_read  = tmpfs_file_can_read,
        .can_write = tmpfs_file_can_write,
        .eof       = tmpfs_file_eof,
    },
};

MODULE_INIT(tmpfs, tmpfs_init, NULL)
