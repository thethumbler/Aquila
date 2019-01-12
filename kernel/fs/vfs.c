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

static int vfs_log_level = LOG_NONE;
static inline DEF_LOGGER(vfs, vfs_log, vfs_log_level)

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

char **canonicalize_path(const char * const path)
{
    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(path, '/');
    return tokens;
}

int vfs_parse_path(const char *path, struct uio *uio, char **abs_path)
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
            if (*token)
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

struct vfs_path *vfs_get_mountpoint(char **tokens)
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
    vfs_log(LOG_INFO, "Initializing\n");
}

int vfs_install(struct fs *fs)
{
    struct fs_list *node = kmalloc(sizeof(struct fs_list));

    if (!node)
        return -ENOMEM;

    node->name = fs->name;
    node->fs   = fs;
    node->next = registered_fs;

    registered_fs = node;

    vfs_log(LOG_INFO, "Registered filesystem %s\n", fs->name);

    return 0;
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
    cur.ino  = p->mountpoint->ino;
    cur.mode = S_IFDIR; /* XXX */
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
    if (S_ISLNK(cur.mode) && !(uio->flags & O_NOFOLLOW)) {
        /* TODO enforce limit */
        //char sym[64];
        char *sym = kmalloc(1024);
        memset(sym, 0, 1024);
        struct inode *inode;
        vfs_vget(&cur, &inode);
        vfs_read(inode, 0, 1024, sym);
        ret = vfs_lookup(sym, uio, vnode, NULL);
        kfree(sym);
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

int vfs_vfind(struct vnode *parent, const char *name, struct vnode *child)
{
    if (!parent || !parent->super || !parent->super->fs)
        return -EINVAL;

    if (!parent->super->fs->iops.vfind)
        return -ENOSYS;

    if (!S_ISDIR(parent->mode))
        return -ENOTDIR;

    return parent->super->fs->iops.vfind(parent, name, child);
}

/* ================== VFS vnode ops mappings ================== */

int vfs_vmknod(struct vnode *dir, const char *name, mode_t mode, dev_t dev, struct uio *uio, struct inode **ref)
{
    /* Invalid request */
    if (!dir || !dir->super || !dir->super->fs)
        return -EINVAL;

    /* Not a directory */
    if (!S_ISDIR(dir->mode))
        return -ENOTDIR;

    /* Operation not supported */
    if (!dir->super->fs->iops.vmknod)
        return -ENOSYS;

    int ret = dir->super->fs->iops.vmknod(dir, name, mode, dev, uio, ref);

    if (!ret && ref && *ref)
        (*ref)->ref++;

    return ret;
}

int vfs_vcreat(struct vnode *dir, const char *name, struct uio *uio, struct inode **ref)
{
    return vfs_vmknod(dir, name, S_IFREG, 0, uio, ref);
}

int vfs_vmkdir(struct vnode *dir, const char *name, struct uio *uio, struct inode **ref)
{
    return vfs_vmknod(dir, name, S_IFDIR, 0, uio, ref);
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
    struct inode *inode = vmr->inode;

    if (!inode || !inode->fs)
        return -EINVAL;

    if (ISDEV(inode))
        return kdev_mmap(&INODE_DEV(inode), vmr);

    if (!inode->fs->iops.mmap)
        return -ENOSYS;

    return inode->fs->iops.mmap(vmr);
}

/* ================== VFS high level mappings ================== */

int vfs_perms_check(struct file *file, struct uio *uio)
{
    if (uio->uid == 0) {    /* Root */
        return 0;
    }

    mode_t mode = file->inode->mode;
    uid_t  uid  = file->inode->uid;
    gid_t  gid  = file->inode->gid;

read_perms:
    /* Read permissions */
    if ((file->flags & O_ACCMODE) == O_RDONLY && (file->flags & O_ACCMODE) != O_WRONLY) {
        if (uid == uio->uid) {
            if (mode & S_IRUSR)
                goto write_perms;
        } else if (gid == uio->gid) {
            if (mode & S_IRGRP)
                goto write_perms;
        } else {
            if (mode & S_IROTH)
                goto write_perms;
        }

        return -EACCES;
    }

write_perms:
    /* Write permissions */
    if (file->flags & (O_WRONLY | O_RDWR)) { 
        if (uid == uio->uid) {
            if (mode & S_IWUSR)
                goto exec_perms;
        } else if (gid == uio->gid) {
            if (mode & S_IWGRP)
                goto exec_perms;
        } else {
            if (mode & S_IWOTH)
                goto exec_perms;
        }

        return -EACCES;
    }

exec_perms:
    /* Execute permissions */
    if (file->flags & O_EXEC) { 
        if (uid == uio->uid) {
            if (mode & S_IXUSR)
                goto done;
        } else if (gid == uio->gid) {
            if (mode & S_IXGRP)
                goto done;
        } else {
            if (mode & S_IXOTH)
                goto done;
        }

        return -EACCES;
    }
    
done:
    return 0;
}
