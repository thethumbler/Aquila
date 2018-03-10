#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <bits/fcntl.h>
#include <bits/errno.h>

/* List of registered filesystems */
struct fs_list {
    const char *name;
    struct fs  *fs;
    struct fs_list *next;
} *registered_fs = NULL;

/* VFS mountpoints graph */
struct vfs_node {
    const char *name;
    struct vfs_node *children;
    struct vfs_node *next;

    /* Reference to node */
    struct inode *node;
} vfs_graph = {
    .name = "/",
};

/* ================== VFS Graph helpers ================== */

struct inode *vfs_root = NULL;
static void vfs_mount_root(struct inode *node)
{
    /* TODO Flush mountpoints */
    vfs_root = node;
    vfs_graph.node = node;
    //vfs_graph_cache_node(&vfs_graph, node);
}

static char **canonicalize_path(const char * const path)
{
    printk("canonicalize_path(path=%s)\n", path);

    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(path, '/');

    return tokens;
}

static int vfs_relative_path(const char * const rel, const char * const path, char **abs_path)
{
    printk("parse_relative_path(rel=%s, path=%s)\n", rel, path);
    size_t rel_len = strlen(rel), path_len = strlen(path);
    char *buf = kmalloc(rel_len + path_len + 2);

    memcpy(buf, rel, rel_len);
    if (rel[rel_len-1] == '/') {
        memcpy(buf + rel_len, path, path_len);
        buf[rel_len + path_len] = 0;
    } else {
        buf[rel_len] = '/';
        memcpy(buf + rel_len + 1, path, path_len);
        buf[rel_len + path_len + 1] = 0;
    }

    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(buf, '/');
    char *out = kmalloc(rel_len + path_len + 1);

    char *valid_tokens[512];
    size_t i = 0;

    foreach (token, tokens) {
        if (token[0] == '.') {
            if (token[1] == '.' && i > 0)
                valid_tokens[--i] = NULL;
        } else {
            valid_tokens[i++] = token;
        }
    }

    valid_tokens[i] = NULL;

    out[0] = '/';
    size_t j = 1;
    foreach (token, valid_tokens) {
        size_t len = strlen(token);
        memcpy(out + j, token, len);
        j += len;
        out[j] = '/';
        ++j;
    }

    out[j > 1? --j : 1] = 0;

    free_tokens(tokens);
    kfree(tokens);
    kfree(buf);

    if (abs_path)
        *abs_path = out;
    else
        kfree(out);

    return 0;
}

static struct vfs_path *vfs_get_mountpoint(char **tokens)
{
    //printk("vfs_get_mountpoint(tokens=%p)\n", tokens);
    struct vfs_path *path = kmalloc(sizeof(struct vfs_path));
    path->tokens = tokens;

    struct vfs_node *cur_node = &vfs_graph;
    struct vfs_node *last_target_node = cur_node;

    size_t token_i = 0;
    int check_last_node = 0;

    foreach (token, tokens) {
        check_last_node = 0;

        if (cur_node->node) {
            last_target_node = cur_node;
            path->tokens = tokens + token_i;
        }

        if (cur_node->children) {
            cur_node = cur_node->children;
            forlinked (m_node, cur_node, m_node->next) {
                if (!strcmp(token, m_node->name)) {
                    cur_node = m_node;
                    check_last_node = 1;
                    goto next;
                }
            }
            /* Not found, break! */
            break;
        } else {
            /* No children, break! */
            break;
        }
next:;
        ++token_i;
    }

    if (check_last_node && cur_node->node) {
        last_target_node = cur_node;
        path->tokens = tokens + token_i;
    }

    path->mountpoint = last_target_node->node;

    return path;
}

/*  Bind VFS path to node */
static int vfs_bind(const char *path, struct inode *target)
{
    //printk("vfs_mount(path=%s, target=%p)\n", path, target);

    /* if path is NULL pointer, or path is empty string, or no target return -1 */
    if (!path ||  !*path || !target)
        return -EINVAL;

    /* Canonicalize Path */
    char **tokens = canonicalize_path(path);
    
    /* FIXME: should check for existence */

    struct vfs_node *cur_node = &vfs_graph;

    foreach (token, tokens) {
        if (cur_node->children) {
            cur_node = cur_node->children;

            /* Look for token in node children */
            struct vfs_node *last_node = NULL;
            forlinked (node, cur_node, node->next) {
                last_node = node;
                if (!strcmp(node->name, token)) {   /* Found */
                    cur_node = node;
                    goto next;
                }
            }

            /* Not found, create it */
            //printk("%s not found, creating it on %s\n", token, last_node->name);
            struct vfs_node *new_node = kmalloc(sizeof(struct vfs_node));
            memset(new_node, 0, sizeof(struct vfs_node));
            new_node->name = strdup(token);
            last_node->next = new_node;
            cur_node = new_node;
        } else {
            //printk("no children found, creating %s on %s\n", token, cur_node->name);
            struct vfs_node *new_node = kmalloc(sizeof(struct vfs_node));
            memset(new_node, 0, sizeof(struct vfs_node));
            new_node->name = strdup(token);
            cur_node->children = new_node;
            cur_node = new_node;
        }
next:;
    }

    cur_node->node = target;
    return 0;
}

static void vfs_init(void)
{
    //extern struct fs ext2fs;
    extern struct fs devfs;
    extern struct fs devpts;

    struct fs *fs_list[] = {
        &devfs,
        &devpts,
        //&ext2fs,
        NULL
    };

    foreach (fs, fs_list) {
        vfs.install(fs);
    }
}

static void vfs_install(struct fs *fs)
{
    //printk("\tVFS install %s ... ", fs->name);

    int err;
    if ((err = fs->init())) {
        //printk("Error %d\n", err);
    } else {
        struct fs_list *node = kmalloc(sizeof(struct fs_list));
        node->name = fs->name;
        node->fs = fs;
        node->next = registered_fs;
        registered_fs = node;
        //printk("Success\n");
    }
}

static int vfs_lookup(const char *path, uint32_t uid, uint32_t gid, struct vnode *vnode)
{
    printk("vfs_lookup(path=%s, uid=%d, gid=%d, vnode=%p)\n", path, uid, gid, vnode);

    int ret = 0;

    /* if path is NULL pointer, or path is empty string, return NULL */
    if (!path ||  !*path)
        return -ENOENT;

    /* Canonicalize Path */
    char **tokens = canonicalize_path(path);

    /* Get mountpoint & path */
    struct vfs_path *p = vfs_get_mountpoint(tokens);

    struct vnode cur, next;

    cur.super  = p->mountpoint;
    cur.id     = p->mountpoint->id;
    cur.type   = FS_DIR; /* XXX */
    next.super = p->mountpoint;

    foreach (token, p->tokens) {
        if ((ret = vfs.vfind(&cur, token, &next)))
            goto error;
        memcpy(&cur, &next, sizeof(cur));
    }

free_resources:
    free_tokens(tokens);
    kfree(p);
    memcpy(vnode, &cur, sizeof(cur));
    return 0;
error:
    if (tokens)
        free_tokens(tokens);
    if (p)
        kfree(p);
    return ret;
}

/* ================== VFS inode ops mappings ================== */

static ssize_t vfs_read(struct inode *inode, size_t offset, size_t size, void *buf)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.read)
        return -ENOSYS;

    return inode->fs->iops.read(inode, offset, size, buf);
}

static ssize_t vfs_write(struct inode *inode, size_t offset, size_t size, void *buf)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.write)
        return -ENOSYS;

    return inode->fs->iops.write(inode, offset, size, buf);
}

static int vfs_create(struct vnode *dir, const char *name, int uid, int gid, int mask, struct inode **node)
{
    /* Invalid request */
    if (!dir || !dir->super || !dir->super->fs)
        return -EINVAL;

    /* Not a directory */
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->super->fs->iops.create)
        return -ENOSYS;

    return dir->super->fs->iops.create(dir, name, uid, gid, mask, node);
}

static int vfs_mkdir(struct vnode *dir, const char *dname, int uid, int gid, int mode)
{
    /* Invalid request */
    if (!dir || !dir->super || !dir->super->fs)
        return -EINVAL;

    /* Not a directory */
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->super->fs->iops.mkdir)
        return -ENOSYS;

    return dir->super->fs->iops.mkdir(dir, dname, uid, gid, mode);
}

static int vfs_ioctl(struct inode *inode, unsigned long request, void *argp)
{
    /* TODO Basic ioctl handling */
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.ioctl)
        return -ENOSYS;

    return inode->fs->iops.ioctl(inode, request, argp);
}

static ssize_t vfs_readdir(struct inode *inode, off_t offset, struct dirent *dirent)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.readdir)
        return -ENOSYS;

    return inode->fs->iops.readdir(inode, offset, dirent);
}


static int vfs_mount(const char *type, const char *dir, int flags, void *data)
{
    //printk("vfs_mount_type(type=%s, dir=%s, flags=%x, data=%p)\n", type, dir, flags, data);

    struct fs *fs = NULL;

    /* Look up filesystem */
    forlinked (entry, registered_fs, entry->next) {
        if (!strcmp(entry->name, type)) {
            fs = entry->fs;
            break;
        }
    }

    if (!fs)
        return -EINVAL;

    return fs->mount(dir, flags, data);
}

/*static int vfs_open(struct fs_node *file, int flags)
{
    if(!file || !file->fs || !file->fs->open)
        return -1;
    return file->fs->open(file, flags);
}*/


/* VFS Generic File function */
int generic_file_open(struct file *file __unused)
{
    return 0;
}

static int vfs_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    if (!parent || !parent->super || !parent->super->fs)
        return -EINVAL;

    if (!parent->super->fs->iops.vfind)
        return -ENOSYS;

    return parent->super->fs->iops.vfind(parent, name, child);
}

/* ================== VFS file ops mappings ================== */

static int vfs_file_open(struct file *file)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.open)
        return -ENOSYS;

    return file->node->fs->fops.open(file);
}

static ssize_t vfs_file_read(struct file *file, void *buf, size_t nbytes)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.read)
        return -ENOSYS;

    return file->node->fs->fops.read(file, buf, nbytes);
}

static ssize_t vfs_file_write(struct file *file, void *buf, size_t nbytes)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.write)
        return -ENOSYS;

    return file->node->fs->fops.write(file, buf, nbytes);
}

static int vfs_file_ioctl(struct file *file, int request, void *argp)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.ioctl)
        return -ENOSYS;

    return file->node->fs->fops.ioctl(file, request, argp);
}

static off_t vfs_file_lseek(struct file *file, off_t offset, int whence)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.lseek)
        return -ENOSYS;

    return file->node->fs->fops.lseek(file, offset, whence);
}

static ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (file->node->type != FS_DIR)
        return -ENOTDIR;

    if (!file->node->fs->fops.readdir)
        return -ENOSYS;

    return file->node->fs->fops.readdir(file, dirent);
}

static ssize_t vfs_file_close(struct file *file)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.close)
        return -ENOSYS;

    return file->node->fs->fops.close(file);
}

static int vfs_file_can_read(struct file *file, size_t size)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.can_read)
        return -ENOSYS;

    return file->node->fs->fops.can_read(file, size);
}

static int vfs_file_can_write(struct file *file, size_t size)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.can_write)
        return -ENOSYS;

    return file->node->fs->fops.can_write(file, size);
}

static int vfs_file_eof(struct file *file)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.eof)
        return -ENOSYS;

    return file->node->fs->fops.eof(file);
}

/* ================== VFS vnode ops mappings ================== */

static int vfs_vrelease(struct vnode *vnode)
{
    kfree(vnode);
    return 0;
}

static int vfs_vget(struct vnode *vnode, struct inode **inode)
{
    if (!vnode || !vnode->super || !vnode->super->fs)
        return -EINVAL;

    if (!vnode->super->fs->iops.vget)
        return -ENOSYS;

    return vnode->super->fs->iops.vget(vnode, inode);
}

struct vfs vfs = (struct vfs) {
    /* Filesystem operations */
    .init       = vfs_init,
    .install    = vfs_install,
    .mount_root = vfs_mount_root,
    .bind       = vfs_bind,

    /* Inode operations mapper */
    .create  = vfs_create,
    .mkdir   = vfs_mkdir,
    .read    = vfs_read,
    .write   = vfs_write,
    .ioctl   = vfs_ioctl,
    .readdir = vfs_readdir,

    /* file operations mappings */
    .fops = {
        .open    = vfs_file_open,
        .read    = vfs_file_read,
        .write   = vfs_file_write,
        .ioctl   = vfs_file_ioctl,
        .lseek   = vfs_file_lseek,
        .readdir = vfs_file_readdir,
        .close   = vfs_file_close,

        /* helpers */
        .can_read  = vfs_file_can_read,
        .can_write = vfs_file_can_write,
        .eof       = vfs_file_eof,
    },

    /* Path resolution and lookup */
    .lookup    = vfs_lookup,
    .vfind     = vfs_vfind,
    .vget      = vfs_vget,
    .relative  = vfs_relative_path,
};
