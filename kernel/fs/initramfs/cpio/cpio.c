#include <core/system.h>
#include <core/module.h>
#include <core/panic.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <fs/posix.h>
#include <fs/rofs.h>

#include <cpio.h>

MALLOC_DEFINE(M_CPIO, "cpio", "CPIO structure");

static int cpio_root_node(struct vnode *super, struct vnode **ref)
{
    int err = 0;
    struct vnode *vnode = NULL;
    struct cpio *p = NULL;

    vnode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (vnode == NULL) {
        err = -ENOMEM;
        goto error;
    }

    p = kmalloc(sizeof(struct cpio), &M_CPIO, 0);
    if (p == NULL) {
        err = -ENOMEM;
        goto error;
    }

    vnode->ino   = (ino_t) vnode;
    vnode->size  = 0;
    vnode->mode  = S_IFDIR | 0775;
    vnode->uid   = 0;
    vnode->gid   = 0;
    vnode->nlink = 2;
    vnode->ref   = 1;

    struct timespec ts;
    gettime(&ts);

    vnode->ctime = ts;
    vnode->atime = ts;
    vnode->mtime = ts;

    vnode->fs    = &cpiofs;
    vnode->p     = p;

    p->super     = super;
    p->parent    = NULL;
    p->dir       = NULL;
    p->count     = 0;
    p->data      = 0;
    p->next      = NULL;

    if (ref) *ref = vnode;

    return 0;

error:
    if (p)
        kfree(p);
    if (vnode)
        kfree(vnode);

    return err;
}

static int cpio_new_node(const char *name, struct cpio_hdr *hdr, size_t sz, size_t data, struct vnode *sp, struct vnode **ref)
{
    int err = 0;
    struct vnode *vnode = NULL;
    struct cpio *p = NULL;

    vnode = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);

    if (vnode == NULL) {
        err = -ENOMEM;
        goto error;
    }

    p = kmalloc(sizeof(struct cpio), &M_CPIO, 0);
    if (p == NULL) {
        err = -ENOMEM;
        goto error;
    }

    vnode->ino   = (vino_t) vnode;
    vnode->size  = sz;
    vnode->uid   = 0;
    vnode->gid   = 0;
    vnode->mode  = hdr->mode;
    vnode->nlink = hdr->nlink;
    vnode->mtime = (struct timespec) {.tv_sec  = hdr->mtime[0] * 0x10000 + hdr->mtime[1], .tv_nsec = 0};
    vnode->rdev  = hdr->rdev;

    vnode->fs   = &cpiofs;
    vnode->p    = p;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = data;
    p->next   = NULL;
    p->name   = strdup(name);

    if (ref) *ref = vnode;

    return 0;

error:
    if (p)
        kfree(p);
    if (vnode)
        kfree(vnode);
    return err;
}

static struct vnode *cpio_new_child_node(struct vnode *parent, struct vnode *child)
{
    if (!parent || !child)  /* invalid vnode */
        return NULL;

    if (!S_ISDIR(parent->mode)) /* Adding child to non directory parent */
        return NULL;

    struct cpio *pp = (struct cpio *) parent->p;
    struct cpio *cp = (struct cpio *) child->p;

    struct vnode *tmp = pp->dir;

    cp->next = tmp;
    pp->dir = child;

    pp->count++;
    cp->parent = parent;

    return child;
}

static struct vnode *cpio_find(struct vnode *root, const char *path)
{
    char **tokens = tokenize(path, '/');

    if (!S_ISDIR(root->mode))   /* Not even a directory */
        return NULL;

    struct vnode *cur = root;
    struct vnode *dir = ((struct cpio *) cur->p)->dir;

    if (!dir) { /* Directory has no children */
        if (*tokens == NULL)
            return root;
        else
            return NULL;
    }

    int flag;
    for (char **token_p = tokens; *token_p; ++token_p) {
        char *token = *token_p;

        flag = 0;
        for (struct vnode *e = dir; e; e = ((struct cpio *) e->p)->next) {
            if (((struct cpio *) e->p)->name &&
                    !strcmp(((struct cpio *) e->p)->name, token)) {
                cur = e;
                dir = ((struct cpio *) e->p)->dir;
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

static int cpio_finddir(struct vnode *root, const char *name, struct dirent *dirent)
{
    if (!S_ISDIR(root->mode))   /* Not even a directory */
        return -ENOTDIR;

    struct vnode *dir = ((struct cpio *) root->p)->dir;

    if (!dir)   /* Directory has no children */
        return -ENOENT;

    for (struct vnode *e = dir; e; e = ((struct cpio *) e->p)->next) {
        if (((struct cpio *) e->p)->name &&
                !strcmp(((struct cpio *) e->p)->name, name)) {
            if (dirent) {
                dirent->d_ino = (vino_t) e;
                strcpy(dirent->d_name, name);
            }

            return 0;
        }
    }

    return -ENOENT;
}

static int cpio_vget(struct vnode *super, ino_t ino, struct vnode **vnode)
{
    *vnode = (struct vnode *) ino;
    return 0;
}

#define MAX_NAMESIZE    1024

static int cpio_load(struct vnode *dev, struct vnode **super)
{
    /* Allocate the root node */
    struct vnode *rootfs = NULL;
    cpio_root_node(dev, &rootfs);

    struct cpio_hdr hdr;
    size_t offset = 0;
    size_t size = 0;

    for (; offset < dev->size; 
          offset += sizeof(struct cpio_hdr) + (hdr.namesize+1)/2*2 + (size+1)/2*2) {

        size_t data_offset = offset;
        vfs_read(dev, data_offset, sizeof(struct cpio_hdr), &hdr);

        if (hdr.magic != CPIO_BIN_MAGIC) { /* Invalid CPIO archive */
            panic("Invalid CPIO archive\n");
            //return -EINVAL;
        }

        size = hdr.filesize[0] * 0x10000 + hdr.filesize[1];
        
        data_offset += sizeof(struct cpio_hdr);
        
        char path[MAX_NAMESIZE];
        vfs_read(dev, data_offset, hdr.namesize, path);

        if (!strcmp(path, ".")) continue;
        if (!strcmp(path, "TRAILER!!!")) break; /* End of archive */

        char *dir  = NULL;
        char *name = NULL;

        /* TODO implement strrchr */
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

        struct vnode *_node = NULL; 
        cpio_new_node(strdup(name), &hdr, size, data_offset, dev, &_node);

        struct vnode *parent = cpio_find(rootfs, dir);
        cpio_new_child_node(parent, _node);
    }

    if (super)
        *super = rootfs;

    return 0;
}

static ssize_t cpio_read(struct vnode *vnode, off_t offset, size_t len, void *buf)
{
    if ((size_t) offset >= vnode->size)
        return 0;

    len = MIN(len, vnode->size - offset);
    struct cpio *p = vnode->p;
    struct vnode *super = p->super;

    return vfs_read(super, p->data + offset, len, buf);
}

static ssize_t cpio_readdir(struct vnode *node, off_t offset, struct dirent *dirent)
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

    struct cpio *p = (struct cpio *) node->p;

    if ((size_t) offset == p->count)
        return 0;

    int i = 0;
    struct vnode *dir = p->dir;

    for (struct vnode *e = dir; e; e = ((struct cpio *) e->p)->next) {
        if (i == offset) {
            dirent->d_ino = (size_t) e;
            strcpy(dirent->d_name, ((struct cpio *) e->p)->name);   // FIXME
            break;
        }
        ++i;
    }

    return i == offset;
}

static int cpio_close(struct vnode *vnode)
{
    return 0;
}

static int cpio_eof(struct file *file)
{
    if (S_ISDIR(file->vnode->mode)) {
        return (size_t) file->offset >= ((struct cpio *) file->vnode->p)->count;
    } else {
        return (size_t) file->offset >= file->vnode->size;
    }
}

static int cpio_init()
{
    return initramfs_archiver_register(&cpiofs);
}

struct fs cpiofs = {
    .name = "cpio",
    .load = cpio_load,

    .vops = {
        .read    = cpio_read,
        .readdir = cpio_readdir,
        .finddir = cpio_finddir,
        .vget    = cpio_vget,
        .close   = cpio_close,

        .write   = rofs_write,
        .trunc   = rofs_trunc,
        .vmknod  = rofs_vmknod,
        .vunlink = rofs_vunlink,
    },
    
    .fops = {
        .open    = posix_file_open,
        .close   = posix_file_close,
        .read    = posix_file_read,
        .write   = posix_file_write,
        .readdir = posix_file_readdir,
        .lseek   = posix_file_lseek,

        .eof     = cpio_eof,
    },
};

MODULE_INIT(initramfs_cpio, cpio_init, NULL)
