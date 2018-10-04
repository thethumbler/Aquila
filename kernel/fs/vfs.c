#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <bits/fcntl.h>
#include <bits/errno.h>
#include <dev/dev.h>
#include <net/socket.h>

/* List of registered filesystems */
struct fs_list *registered_fs = NULL;

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
void vfs_mount_root(struct inode *node)
{
    /* TODO Flush mountpoints */
    vfs_root = node;
    vfs_graph.node = node;
    vfs_graph.children = NULL;  /* XXX */
}

static char **canonicalize_path(const char * const path)
{
    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(path, '/');
    return tokens;
}

static int vfs_parse_path(const char *path, struct uio *uio, char **abs_path)
{
    if (!path || !*path)
        return -ENOENT;

    char *cwd = uio->cwd;

    if (*path == '/') { /* Absolute path */
        cwd = "/";
    }

    size_t cwd_len = strlen(cwd), path_len = strlen(path);
    char *buf = kmalloc(cwd_len + path_len + 2);

    memcpy(buf, cwd, cwd_len);

    buf[cwd_len] = '/';
    memcpy(buf + cwd_len + 1, path, path_len);
    buf[cwd_len + path_len + 1] = 0;

    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(buf, '/');
    char *out = kmalloc(cwd_len + path_len + 1);

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
int vfs_bind(const char *path, struct inode *target)
{
    /* if path is NULL pointer, or path is empty string, or no target return EINVAL */
    if (!path ||  !*path || !target)
        return -EINVAL;

    if (!strcmp(path, "/")) {
        vfs_mount_root(target);
        return 0;
    }

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
            struct vfs_node *new_node = kmalloc(sizeof(struct vfs_node));
            memset(new_node, 0, sizeof(struct vfs_node));
            new_node->name = strdup(token);
            last_node->next = new_node;
            cur_node = new_node;
        } else {
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

void vfs_init(void)
{
    printk("vfs: Initializing\n");
}

void vfs_install(struct fs *fs)
{
    struct fs_list *node = kmalloc(sizeof(struct fs_list));
    node->name = fs->name;
    node->fs = fs;
    node->next = registered_fs;
    registered_fs = node;
    printk("vfs: Registered filesystem %s\n", fs->name);
}

int vfs_lookup(const char *path, struct uio *uio, struct vnode *vnode, char **abs_path)
{
    int ret = 0;
    struct vfs_path *p = NULL;
    char **tokens = NULL;

    /* if path is NULL pointer, or path is empty string, return ENOENT */
    if (!path ||  !*path)
        return -ENOENT;

    char *_path = NULL;
    if ((ret = vfs_parse_path(path, uio, &_path)))
        goto error;

    /* Canonicalize Path */
    tokens = canonicalize_path(_path);

    /* Get mountpoint & path */
    p = vfs_get_mountpoint(tokens);

    struct vnode cur, next;

    cur.super  = p->mountpoint;
    cur.id     = p->mountpoint->id;
    cur.type   = FS_DIR;
    next.super = p->mountpoint;

    foreach (token, p->tokens) {
        if ((ret = vfs_vfind(&cur, token, &next)))
            goto error;
        memcpy(&cur, &next, sizeof(cur));
    }

    free_tokens(tokens);
    kfree(p);
    memcpy(vnode, &cur, sizeof(cur));

    if (abs_path)
        *abs_path = strdup(_path);

    kfree(_path);

    /* Resolve symbolic links */
    if (cur.type == FS_SYMLINK && !(uio->flags & O_NOFOLLOW)) {
        /* TODO enforce limit */
        char sym[1024];
        struct inode *inode;
        vfs_vget(&cur, &inode);
        vfs_read(inode, 0, 1024, sym);
        int ret = vfs_lookup(sym, uio, vnode, NULL);
        vfs_close(inode);
        return ret;
    }

    return 0;

error:
    if (tokens)
        free_tokens(tokens);
    if (p)
        kfree(p);
    if (_path)
        kfree(_path);

    return ret;
}

/* ================== VFS inode ops mappings ================== */

#define ISDEV(inode) ((inode)->type == FS_CHRDEV || (inode)->type == FS_BLKDEV)

ssize_t vfs_read(struct inode *inode, size_t offset, size_t size, void *buf)
{
    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_read(&_INODE_DEV(inode), offset, size, buf);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.read)
        return -ENOSYS;

    return inode->fs->iops.read(inode, offset, size, buf);
}

ssize_t vfs_write(struct inode *inode, size_t offset, size_t size, void *buf)
{
    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_write(&_INODE_DEV(inode), offset, size, buf);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.write)
        return -ENOSYS;

    return inode->fs->iops.write(inode, offset, size, buf);
}


int vfs_ioctl(struct inode *inode, unsigned long request, void *argp)
{
    /* TODO Basic ioctl handling */
    /* Invalid request */
    if (!inode)
        return -EINVAL;

    /* Device node */
    if (ISDEV(inode))
        return kdev_ioctl(&_INODE_DEV(inode), request, argp);

    /* Invalid request */
    if (!inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.ioctl)
        return -ENOSYS;

    return inode->fs->iops.ioctl(inode, request, argp);
}

ssize_t vfs_readdir(struct inode *inode, off_t offset, struct dirent *dirent)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.readdir)
        return -ENOSYS;

    return inode->fs->iops.readdir(inode, offset, dirent);
}

int vfs_close(struct inode *inode)
{
    /* Invalid request */
    if (!inode || !inode->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!inode->fs->iops.close)
        return -ENOSYS;

    --inode->ref;

    if (inode->ref <= 0) {   /* Why < ? */
        return inode->fs->iops.close(inode);
    }

    return 0;
}

int vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio)
{
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

    /* Directory path must be absolute */
    int ret = 0;
    char *_dir = NULL;
    if ((ret = vfs_parse_path(dir, uio, &_dir))) {
        if (_dir)
            kfree(_dir);
        return ret;
    }

    return fs->mount(_dir, flags, data);
}

int vfs_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    if (!parent || !parent->super || !parent->super->fs)
        return -EINVAL;

    if (!parent->super->fs->iops.vfind)
        return -ENOSYS;

    if (parent->type != FS_DIR)
        return -ENOTDIR;

    return parent->super->fs->iops.vfind(parent, name, child);
}

/* ================== VFS vnode ops mappings ================== */

int vfs_vmknod(struct vnode *dir, const char *name, itype_t type, dev_t dev, struct uio *uio, struct inode **ref)
{
    /* Invalid request */
    if (!dir || !dir->super || !dir->super->fs)
        return -EINVAL;

    /* Not a directory */
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->super->fs->iops.vmknod)
        return -ENOSYS;

    int ret = dir->super->fs->iops.vmknod(dir, name, type, dev, uio, ref);

    if (!ret && ref && *ref)
        (*ref)->ref++;

    return ret;
}

int vfs_vcreat(struct vnode *dir, const char *name, struct uio *uio, struct inode **ref)
{
    return vfs_vmknod(dir, name, FS_RGL, 0, uio, ref);
}

int vfs_vmkdir(struct vnode *dir, const char *name, struct uio *uio, struct inode **ref)
{
    return vfs_vmknod(dir, name, FS_DIR, 0, uio, ref);
}

int vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio)
{
    /* Invalid request */
    if (!dir|| !dir->super || !dir->super->fs)
        return -EINVAL;

    /* Operation not supported */
    if (!dir->super->fs->iops.vunlink)
        return -ENOSYS;

    return dir->super->fs->iops.vunlink(dir, fn, uio);
}

int vfs_vget(struct vnode *vnode, struct inode **inode)
{
    if (!vnode || !vnode->super || !vnode->super->fs)
        return -EINVAL;

    if (!vnode->super->fs->iops.vget)
        return -ENOSYS;

    int ret = vnode->super->fs->iops.vget(vnode, inode);

    if (!ret && inode && *inode) {
        (*inode)->ref++;
    }

    return ret;
}

int vfs_mmap(struct vmr *vmr)
{
    printk("vfs_mmap(vmr=%p)\n", vmr);
    struct inode *inode = vmr->inode;

    if (!inode || !inode->fs)
        return -EINVAL;

    if (ISDEV(inode))
        return kdev_mmap(&_INODE_DEV(inode), vmr);

    if (!inode->fs->iops.mmap)
        return -ENOSYS;

    return inode->fs->iops.mmap(vmr);
}

/* ================== VFS file ops mappings ================== */

int vfs_file_open(struct file *file)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (file->node->type == FS_DIR && !(file->flags & O_SEARCH))
        return -EISDIR;

    if (ISDEV(file->node))
        return kdev_file_open(&_INODE_DEV(file->node), file);

    if (!file->node->fs->fops.open)
        return -ENOSYS;

    return file->node->fs->fops.open(file);
}

ssize_t vfs_file_read(struct file *file, void *buf, size_t nbytes)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_recv(file, buf, nbytes, 0);

    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_read(&_INODE_DEV(file->node), file, buf, nbytes);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.read)
        return -ENOSYS;

    return file->node->fs->fops.read(file, buf, nbytes);
}

ssize_t vfs_file_write(struct file *file, void *buf, size_t nbytes)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_send(file, buf, nbytes, 0);

    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_write(&_INODE_DEV(file->node), file, buf, nbytes);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.write)
        return -ENOSYS;

    return file->node->fs->fops.write(file, buf, nbytes);
}

int vfs_file_ioctl(struct file *file, int request, void *argp)
{
    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_ioctl(&_INODE_DEV(file->node), file, request, argp);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.ioctl)
        return -ENOSYS;

    return file->node->fs->fops.ioctl(file, request, argp);
}

off_t vfs_file_lseek(struct file *file, off_t offset, int whence)
{
    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_lseek(&_INODE_DEV(file->node), file, offset, whence);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.lseek)
        return -ENOSYS;

    return file->node->fs->fops.lseek(file, offset, whence);
}

ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent)
{
    if (!file || !file->node || !file->node->fs)
        return -EINVAL;

    if (file->node->type != FS_DIR)
        return -ENOTDIR;

    if (!file->node->fs->fops.readdir)
        return -ENOSYS;

    return file->node->fs->fops.readdir(file, dirent);
}

ssize_t vfs_file_close(struct file *file)
{
    if (file && file->flags & FILE_SOCKET)
        return socket_shutdown(file, SHUT_RDWR);

    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_close(&_INODE_DEV(file->node), file);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.close)
        return -ENOSYS;

    return file->node->fs->fops.close(file);
}

int vfs_file_can_read(struct file *file, size_t size)
{
    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_can_read(&_INODE_DEV(file->node), file, size);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.can_read)
        return -ENOSYS;

    return file->node->fs->fops.can_read(file, size);
}

int vfs_file_can_write(struct file *file, size_t size)
{
    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_can_write(&_INODE_DEV(file->node), file, size);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.can_write)
        return -ENOSYS;

    return file->node->fs->fops.can_write(file, size);
}

int vfs_file_eof(struct file *file)
{
    if (!file || !file->node)
        return -EINVAL;

    if (ISDEV(file->node))
        return kdev_file_eof(&_INODE_DEV(file->node), file);

    if (!file->node->fs)
        return -EINVAL;

    if (!file->node->fs->fops.eof)
        return -ENOSYS;

    return file->node->fs->fops.eof(file);
}


/* ================== VFS high level mappings ================== */

int vfs_mknod(const char *path, itype_t type, dev_t dev, struct uio *uio, struct inode **ref)
{
    int ret = 0;
    struct vfs_path *p = NULL;
    char **tokens = NULL;

    /* if path is NULL pointer, or path is empty string, return NULL */
    if (!path ||  !*path)
        return -ENOENT;

    char *_path = NULL;
    if ((ret = vfs_parse_path(path, uio, &_path)))
        goto error;

    /* Canonicalize Path */
    tokens = canonicalize_path(_path);

    /* Get mountpoint & path */
    p = vfs_get_mountpoint(tokens);

    struct vnode cur, next;

    cur.super  = p->mountpoint;
    cur.id     = p->mountpoint->id;
    cur.type   = FS_DIR; /* XXX */
    next.super = p->mountpoint;

    char *name = NULL;
    char **tok = p->tokens;

    while (tok) {
        char *token = *tok;

        if (!*(tok + 1)) {
            name = token;
            break;
        }

        if ((ret = vfs_vfind(&cur, token, &next)))
            goto error;

        memcpy(&cur, &next, sizeof(cur));
        ++tok;
    }

    if ((ret = vfs_vmknod(&cur, name, type, dev, uio, ref)))
        goto error;

    free_tokens(tokens);
    kfree(p);
    kfree(_path);
    return 0;

error:
    if (tokens)
        free_tokens(tokens);
    if (p)
        kfree(p);
    if (_path)
        kfree(_path);
    return ret;
}

int vfs_mkdir(const char *path, struct uio *uio, struct inode **ref)
{
    return vfs_mknod(path, FS_DIR, 0, uio, ref);
}

int vfs_creat(const char *path, struct uio *uio, struct inode **ref)
{
    return vfs_mknod(path, FS_RGL, 0, uio, ref);
}

int vfs_stat(struct inode *inode, struct stat *buf)
{
    buf->st_dev   = 0;  /* TODO */
    buf->st_ino   = 0;  /* TODO */

    buf->st_mode = (int []) {
        [FS_RGL]     = _IFREG,
        [FS_DIR]     = _IFDIR,
        [FS_CHRDEV]  = _IFCHR,
        [FS_BLKDEV]  = _IFBLK,
        [FS_SYMLINK] = _IFLNK,
        [FS_PIPE]    = 0,   /* FIXME */
        [FS_FIFO]    = _IFIFO,
        [FS_SOCKET]  = _IFSOCK,
        [FS_SPECIAL] = 0    /* FIXME */
    }[inode->type];

    buf->st_mode  |= inode->mask;

    buf->st_nlink = inode->nlink;
    buf->st_uid   = inode->uid;
    buf->st_gid   = inode->gid;
    buf->st_rdev  = inode->rdev;
    buf->st_size  = inode->size;
    buf->st_mtim  = inode->mtim;

    //memcpy(&buf->st_mtim, &inode->mtim, sizeof(struct timespec));

    return 0;
}

int vfs_unlink(const char *path, struct uio *uio)
{
    int ret = 0;
    struct vfs_path *p = NULL;
    char **tokens = NULL;

    /* if path is NULL pointer, or path is empty string, return NULL */
    if (!path ||  !*path)
        return -ENOENT;

    char *_path = NULL;
    if ((ret = vfs_parse_path(path, uio, &_path)))
        goto error;

    /* Canonicalize Path */
    tokens = canonicalize_path(_path);

    /* Get mountpoint & path */
    p = vfs_get_mountpoint(tokens);

    struct vnode cur, next;

    cur.super  = p->mountpoint;
    cur.id     = p->mountpoint->id;
    cur.type   = FS_DIR;
    next.super = p->mountpoint;

    char *name = NULL;
    char **tok = p->tokens;

    while (tok) {
        char *token = *tok;

        if (!*(tok + 1)) {
            name = token;
            break;
        }

        if ((ret = vfs_vfind(&cur, token, &next)))
            goto error;

        memcpy(&cur, &next, sizeof(cur));
        ++tok;
    }

    if ((ret = vfs_vunlink(&cur, name, uio)))
        goto error;

    free_tokens(tokens);
    kfree(p);
    kfree(_path);
    return 0;

error:
    if (tokens)
        free_tokens(tokens);
    if (p)
        kfree(p);
    if (_path)
        kfree(_path);
    return ret;
}

int vfs_perms_check(struct file *file, struct uio *uio)
{
    if (uio->uid == 0) {    /* Root */
        return 0;
    }

    uint32_t mask = file->node->mask;
    uint32_t uid  = file->node->uid;
    uint32_t gid  = file->node->gid;

read_perms:
    /* Read permissions */
    if ((file->flags & O_ACCMODE) == O_RDONLY && (file->flags & O_ACCMODE) != O_WRONLY) {
        if (uid == uio->uid) {
            if (mask & S_IRUSR)
                goto write_perms;
        } else if (gid == uio->gid) {
            if (mask & S_IRGRP)
                goto write_perms;
        } else {
            if (mask & S_IROTH)
                goto write_perms;
        }

        return -EACCES;
    }

write_perms:
    /* Write permissions */
    if (file->flags & (O_WRONLY | O_RDWR)) { 
        if (uid == uio->uid) {
            if (mask & S_IWUSR)
                goto exec_perms;
        } else if (gid == uio->gid) {
            if (mask & S_IWGRP)
                goto exec_perms;
        } else {
            if (mask & S_IWOTH)
                goto exec_perms;
        }

        return -EACCES;
    }

exec_perms:
    /* Execute permissions */
    if (file->flags & O_EXEC) { 
        if (uid == uio->uid) {
            if (mask & S_IXUSR)
                goto done;
        } else if (gid == uio->gid) {
            if (mask & S_IXGRP)
                goto done;
        } else {
            if (mask & S_IXOTH)
                goto done;
        }

        return -EACCES;
    }
    
done:
    return 0;
}
