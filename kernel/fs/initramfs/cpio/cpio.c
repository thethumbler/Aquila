#include <core/system.h>
#include <core/module.h>
#include <core/panic.h>
#include <core/time.h>

#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <fs/posix.h>

#include <cpio.h>

MALLOC_DEFINE(M_CPIO, "cpio", "CPIO structure");

static int cpio_root_inode(struct inode *super, struct inode **ref)
{
    int err = 0;
    struct inode *inode = NULL;
    struct cpio *p = NULL;

    inode = kmalloc(sizeof(struct inode), &M_INODE, 0);
    if (inode == NULL) {
        err = -ENOMEM;
        goto error;
    }

    p = kmalloc(sizeof(struct cpio), &M_CPIO, 0);
    if (p == NULL) {
        err = -ENOMEM;
        goto error;
    }

    inode->ino   = (ino_t) inode;
    inode->size  = 0;
    inode->mode  = S_IFDIR | 0775;
    inode->uid   = 0;
    inode->gid   = 0;
    inode->nlink = 2;
    inode->ref   = 1;

    struct timespec ts;
    gettime(&ts);

    inode->ctime = ts;
    inode->atime = ts;
    inode->mtime = ts;

    //inode->name  = NULL;
    inode->fs    = &cpio;
    inode->p     = p;


    p->super     = super;
    p->parent    = NULL;
    p->dir       = NULL;
    p->count     = 0;
    p->data      = 0;
    p->next      = NULL;

    if (ref)
        *ref = inode;

    return 0;

error:
    if (p)
        kfree(p);
    if (inode)
        kfree(inode);

    return err;
}

static int cpio_new_inode(const char *name, struct cpio_hdr *hdr, size_t sz, size_t data, struct inode *sp, struct inode **ref)
{
    int err = 0;
    struct inode *inode = NULL;
    struct cpio *p = NULL;

    inode = kmalloc(sizeof(struct inode), &M_INODE, 0);

    if (inode == NULL) {
        err = -ENOMEM;
        goto error;
    }

    memset(inode, 0, sizeof(struct inode));

    p = kmalloc(sizeof(struct cpio), &M_CPIO, 0);
    if (p == NULL) {
        err = -ENOMEM;
        goto error;
    }

    inode->ino   = (vino_t) inode;
    inode->size  = sz;
    inode->uid   = 0;
    inode->gid   = 0;
    inode->mode  = hdr->mode;
    inode->nlink = hdr->nlink;
    inode->mtime = (struct timespec) {.tv_sec  = hdr->mtime[0] * 0x10000 + hdr->mtime[1], .tv_nsec = 0};
    inode->rdev  = hdr->rdev;

    inode->fs   = &cpio;
    inode->p    = p;

    p->super  = sp;
    p->parent = NULL;
    p->dir    = NULL;
    p->count  = 0;
    p->data   = data;
    p->next   = NULL;
    p->name   = strdup(name);

    if (ref)
        *ref = inode;

    return 0;

error:
    if (p)
        kfree(p);
    if (inode)
        kfree(inode);
    return err;
}

static struct inode *cpio_new_child_node(struct inode *parent, struct inode *child)
{
    if (!parent || !child)  /* Invalid inode */
        return NULL;

    if (!S_ISDIR(parent->mode)) /* Adding child to non directory parent */
        return NULL;

    struct cpio *pp = (struct cpio *) parent->p;
    struct cpio *cp = (struct cpio *) child->p;

    struct inode *tmp = pp->dir;

    cp->next = tmp;
    pp->dir = child;

    pp->count++;
    cp->parent = parent;

    return child;
}

static struct inode *cpio_find(struct inode *root, const char *path)
{
    char **tokens = tokenize(path, '/');

    if (!S_ISDIR(root->mode))   /* Not even a directory */
        return NULL;

    struct inode *cur = root;
    struct inode *dir = ((struct cpio *) cur->p)->dir;

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
        for (struct inode *e = dir; e; e = ((struct cpio *) e->p)->next) {
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

static int cpio_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    //printk("cpio_vfind(parent=%p, name=%s, child=%p)\n", parent, name, child);

    struct inode *root = (struct inode *) parent->ino;

    if (!S_ISDIR(root->mode))   /* Not even a directory */
        return -ENOTDIR;

    struct inode *dir = ((struct cpio *) root->p)->dir;

    if (!dir)   /* Directory has no children */
        return -ENOENT;

    for (struct inode *e = dir; e; e = ((struct cpio *) e->p)->next) {
        if (((struct cpio *) e->p)->name &&
                !strcmp(((struct cpio *) e->p)->name, name)) {
            if (child) {
                child->ino  = (vino_t) e;
                child->mode = e->mode;
                child->uid  = e->uid;
                child->gid  = e->gid;
            }

            return 0;
        }
    }

    return -ENOENT;
}

static int cpio_vget(struct vnode *vnode, struct inode **inode)
{
    *inode = (struct inode *) vnode->ino;
    return 0;
}

#define MAX_NAMESIZE    1024

static int cpio_load(struct inode *dev, struct inode **super)
{
    /* Allocate the root node */
    struct inode *rootfs = NULL;
    cpio_root_inode(dev, &rootfs);

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

        struct inode *_node = NULL; 
        cpio_new_inode(strdup(name), &hdr, size, data_offset, dev, &_node);

        struct inode *parent = cpio_find(rootfs, dir);
        cpio_new_child_node(parent, _node);
    }

    if (super)
        *super = rootfs;

    return 0;
}

static ssize_t cpio_read(struct inode *inode, off_t offset, size_t len, void *buf)
{
    if ((size_t) offset >= inode->size)
        return 0;

    len = MIN(len, inode->size - offset);
    struct cpio *p = inode->p;
    struct inode *super = p->super;

    return vfs_read(super, p->data + offset, len, buf);
}

static ssize_t cpio_readdir(struct inode *node, off_t offset, struct dirent *dirent)
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
    struct inode *dir = p->dir;

    for (struct inode *e = dir; e; e = ((struct cpio *) e->p)->next) {
        if (i == offset) {
            dirent->d_ino = (size_t) e;
            strcpy(dirent->d_name, ((struct cpio *) e->p)->name);   // FIXME
            break;
        }
        ++i;
    }

    return i == offset;
}

static int cpio_close(struct inode *inode)
{
    return 0;
}

static int cpio_eof(struct file *file)
{
    if (S_ISDIR(file->inode->mode)) {
        return (size_t) file->offset >= ((struct cpio *) file->inode->p)->count;
    } else {
        return (size_t) file->offset >= file->inode->size;
    }
}

static int cpio_init()
{
    return initramfs_archiver_register(&cpio);
}

struct fs cpio = {
    .load = cpio_load,

    .iops = {
        .read    = cpio_read,
        .readdir = cpio_readdir,
        .vfind   = cpio_vfind,
        .vget    = cpio_vget,
        .vmknod  = __vfs_vmknod_rofs,
        .vunlink = __vfs_vunlink_rofs,
        .close   = cpio_close,
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
