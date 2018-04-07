#include <core/system.h>
#include <core/panic.h>
#include <fs/vfs.h>
#include <fs/cpio.h>
#include <fs/posix.h>

static int __cpio_root_inode(struct inode *sp, struct inode **inode)
{
    int err = 0;
    struct inode *node = NULL;
    struct __cpio_priv *p = NULL;

    if (!(node = kmalloc(sizeof(struct inode)))) {
        err = -ENOMEM;
        goto error;
    }

    if (!(p = kmalloc(sizeof(struct __cpio_priv)))) {
        err = -ENOMEM;
        goto error;
    }

    node->id   = (vino_t) node;
    node->name = NULL;
    node->size = 0;
    node->type = FS_DIR;
    node->fs   = &__cpio;
    node->p    = p;

    node->uid  = 0;
    node->gid  = 0;
    node->mask = 0555;  /* r-xr-xr-x */
    node->nlink = 2;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = 0;
    p->next   = NULL;

    if (inode)
        *inode = node;

    return 0;

error:
    if (p)
        kfree(p);
    if (node)
        kfree(node);

    return err;
}

static int __cpio_new_inode(char *name, struct __cpio_hdr *hdr, size_t sz, size_t data, struct inode *sp, struct inode **inode)
{
    int err = 0;
    struct inode *node = NULL;
    struct __cpio_priv *p = NULL;

    if (!(node = kmalloc(sizeof(struct inode)))) {
        err = -ENOMEM;
        goto error;
    }

    memset(node, 0, sizeof(struct inode));

    if (!(p = kmalloc(sizeof(struct __cpio_priv)))) {
        err = -ENOMEM;
        goto error;
    }

    uint32_t type = 0;
    switch (hdr->mode & CPIO_TYPE_MASK) {
        case CPIO_TYPE_RGL:    type = FS_RGL; break;
        case CPIO_TYPE_DIR:    type = FS_DIR; break;
        case CPIO_TYPE_SLINK:  type = FS_SYMLINK; break;
        case CPIO_TYPE_BLKDEV: type = FS_BLKDEV; break;
        case CPIO_TYPE_CHRDEV: type = FS_CHRDEV; break;
        case CPIO_TYPE_FIFO:   type = FS_FIFO; break;
        case CPIO_TYPE_SOCKET: type = FS_SOCKET; break;
    }
    
    node->id   = (vino_t) node;
    node->name = name;
    node->size = sz;
    node->type = type;
    node->fs   = &__cpio;
    node->p    = p;

    node->uid  = 0;
    node->gid  = 0;
    node->mask = hdr->mode & CPIO_PERM_MASK;
    node->nlink = hdr->nlink;

    node->rdev = hdr->rdev;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = data;
    p->next   = NULL;

    if (inode)
        *inode = node;

    return 0;

error:
    if (p)
        kfree(p);
    if (node)
        kfree(node);
    return err;
}

static struct inode *__cpio_new_child_node(struct inode *parent, struct inode *child)
{
    if (!parent || !child)  /* Invalid inode */
        return NULL;

    if (parent->type != FS_DIR) /* Adding child to non directory parent */
        return NULL;

    struct __cpio_priv *pp = (struct __cpio_priv *) parent->p;
    struct __cpio_priv *cp = (struct __cpio_priv *) child->p;

    struct inode *tmp = pp->dir;

    cp->next = tmp;
    pp->dir = child;

    pp->count++;
    cp->parent = parent;

    return child;
}

static struct inode *__cpio_find(struct inode *root, const char *path)
{
    char **tokens = tokenize(path, '/');

    if (root->type != FS_DIR)   /* Not even a directory */
        return NULL;

    struct inode *cur = root;
    struct inode *dir = ((struct __cpio_priv *) cur->p)->dir;

    if (!dir) { /* Directory has no children */
        if (*tokens == NULL)
            return root;
        else
            return NULL;
    }

    int flag;
    foreach (token, tokens) {
        flag = 0;
        forlinked (e, dir, ((struct __cpio_priv *) e->p)->next) {
            if (e->name && !strcmp(e->name, token)) {
                cur = e;
                dir = ((struct __cpio_priv *) e->p)->dir;
                flag = 1;
                goto next;
            }
        }

        next:

        if (!flag)  /* No such file or directory */
            return NULL;

        continue;
    }

    free_tokens(tokens);

    return cur;
}

static int __cpio_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    struct inode *root = (struct inode *) parent->id;

    if (root->type != FS_DIR)   /* Not even a directory */
        return -ENOTDIR;

    struct inode *dir = ((struct __cpio_priv *) root->p)->dir;

    if (!dir)   /* Directory has no children */
        return -ENOENT;

    forlinked (e, dir, ((struct __cpio_priv *) e->p)->next) {
        if (e->name && !strcmp(e->name, name)) {
            if (child) {
                child->id   = (vino_t) e;
                child->type = e->type;
                child->uid  = e->uid;
                child->gid  = e->gid;
                child->mask = e->mask;
            }

            return 0;
        }
    }

    return -ENOENT;
}

static int __cpio_vget(struct vnode *vnode, struct inode **inode)
{
    *inode = (struct inode *) vnode->id;
    return 0;
}

static int __cpio_load(struct inode *dev, struct inode **super)
{
    /* Allocate the root node */
    struct inode *rootfs = NULL;
    __cpio_root_inode(dev, &rootfs);

    struct __cpio_hdr hdr;
    size_t offset = 0;
    size_t size = 0;

    for (; offset < dev->size; 
          offset += sizeof(struct __cpio_hdr) + (hdr.namesize+1)/2*2 + (size+1)/2*2) {

        size_t data_offset = offset;
        vfs_read(dev, data_offset, sizeof(struct __cpio_hdr), &hdr);

        if (hdr.magic != CPIO_BIN_MAGIC) { /* Invalid CPIO archive */
            panic("Invalid CPIO archive\n");
            return -1;
        }

        size = hdr.filesize[0] * 0x10000 + hdr.filesize[1];
        
        data_offset += sizeof(struct __cpio_hdr);
        
        char path[hdr.namesize];
        vfs_read(dev, data_offset, hdr.namesize, path);

        if (!strcmp(path, ".")) continue;
        if (!strcmp(path, "TRAILER!!!")) break; /* End of archive */

        char *dir  = NULL;
        char *name = NULL;

        for (int i = hdr.namesize - 1; i >= 0; --i) {
            if (path[i] == '/') {
                path[i] = '\0';
                name = &path[i+1];
                dir  = path;
                break;
            }
        }

        if (!name) {
            name = path;
            dir  = "/";
        }
        
        data_offset += hdr.namesize + (hdr.namesize % 2);

        struct inode *_node = NULL; 
        __cpio_new_inode(strdup(name), &hdr, size, data_offset, dev, &_node);

        struct inode *parent = __cpio_find(rootfs, dir);
        __cpio_new_child_node(parent, _node);
    }

    if (super)
        *super = rootfs;

    return 0;
}

static ssize_t __cpio_read(struct inode *node, off_t offset, size_t len, void *buf_p)
{
    if (offset >= (off_t) node->size)
        return 0;

    len = MIN(len, node->size - offset);

    struct __cpio_priv *p = node->p;
    struct inode *super = p->super;
    return vfs_read(super, p->data + offset, len, buf_p);
}

static ssize_t __cpio_readdir(struct inode *node, off_t offset, struct dirent *dirent)
{
    if (offset == 0) {
        strcpy(dirent->d_name, ".");
        return 1;
    }

    if (offset == 1) {
        strcpy(dirent->d_name, "..");
        return 1;
    }

    offset -= 2;

    struct __cpio_priv *p = (struct __cpio_priv *) node->p;

    if ((size_t) offset == p->count)
        return 0;

    int i = 0;
    struct inode *dir = p->dir;

    forlinked (e, dir, ((struct __cpio_priv *) e->p)->next) {
        if (i == offset) {
            strcpy(dirent->d_name, e->name);   // FIXME
            break;
        }
        ++i;
    }

    return i == offset;
}

static int __cpio_close(struct inode *inode)
{
    return 0;
}

static int __cpio_eof(struct file *file)
{
    if (file->node->type == FS_DIR) {
        return (size_t) file->offset >= ((struct __cpio_priv *) file->node->p)->count;
    } else {
        return (size_t) file->offset >= file->node->size;
    }
}

struct fs __cpio = {
    .load = __cpio_load,

    .iops = {
        .read    = __cpio_read,
        .readdir = __cpio_readdir,
        .vfind   = __cpio_vfind,
        .vget    = __cpio_vget,
        .vmknod  = __VMKNOD_T __vfs_rofs,
        .vunlink = __vfs_rofs,
        .close   = __cpio_close,
    },
    
    .fops = {
        .open    = posix_file_open,
        .close   = posix_file_close,
        .read    = posix_file_read,
        .write   = posix_file_write,
        .readdir = posix_file_readdir,
        .lseek   = posix_file_lseek,

        .eof     = __cpio_eof,
    },
};
