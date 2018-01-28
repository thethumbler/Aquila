#include <core/system.h>
#include <core/string.h>
#include <mm/mm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <bits/fcntl.h>
#include <bits/errno.h>

struct fs_list {
    const char *name;
    struct fs  *fs;
    struct fs_list *next;
} *registered_fs = NULL;

struct vfs_node {
    const char *name;
    struct vfs_node *children;
    struct vfs_node *next;

    /* We cache type and access permissions of node */
    enum fs_node_type   type;
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;

    /* Reference to node */
    struct fs_node *node;
} vfs_graph = {
    .name = "/",
};

/* ================== VFS Graph helpers ================== */

struct fs_node *vfs_root = NULL;

static void vfs_graph_cache_node(struct vfs_node *vnode, struct fs_node *node)
{
    vnode->type = node->type;
    vnode->mask = node->mask;
    vnode->uid  = node->uid;
    vnode->gid  = node->gid;
}

static void vfs_mount_root(struct fs_node *node)
{
    vfs_root = node;
    vfs_graph.node = node;
    vfs_graph_cache_node(&vfs_graph, node);
}

static char **canonicalize_path(const char * const path)
{
    //printk("canonicalize_path(path=%s)\n", path);

    /* Tokenize slash seperated words in path into tokens */
    char **tokens = tokenize(path, '/');

    /* TODO : Handle ., .. and // */

    return tokens;
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
static int vfs_bind(const char *path, struct fs_node *target)
{
    //printk("vfs_mount(path=%s, target=%p)\n", path, target);

    /* if path is NULL pointer, or path is empty string, or no target return -1 */
    if (!path ||  !*path || !target)
        return -1;

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
    extern struct fs ext2fs;
    extern struct fs devfs;
    extern struct fs devpts;

    struct fs *fs_list[] = {&devfs, &devpts, &ext2fs, NULL};

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

static struct fs_node *vfs_traverse(struct vfs_path *path)
{
    //printk("vfs_traverse(path=%p)\n", path);

    /* if path is NULL pointer return NULL */
    if (!path)
        return NULL;

    return path->mountpoint->fs->traverse(path);
}

static struct fs_node *vfs_find(const char *path)
{
    //printk("vfs_find(path=%s)\n", path);

    /* if path is NULL pointer, or path is empty string, return NULL */
    if (!path ||  !*path)
        return NULL;

    /* Canonicalize Path */
    char **tokens = canonicalize_path(path);

    /* Get mountpoint & path */
    struct vfs_path *p = vfs_get_mountpoint(tokens);

    /* Get fs node */
    struct fs_node *cur = vfs_traverse(p);

free_resources:
    free_tokens(tokens);
    kfree(p);
    return cur;
}

static ssize_t vfs_read(struct fs_node *inode, size_t offset, size_t size, void *buf)
{
    if (!inode) return 0;
    return inode->fs->read(inode, offset, size, buf);
}

static ssize_t vfs_write(struct fs_node *inode, size_t offset, size_t size, void *buf)
{
    if (!inode) return 0;
    return inode->fs->write(inode, offset, size, buf);
}

static int vfs_create(struct fs_node *dir, const char *name)
{
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    return dir->fs->create(dir, name);
}

static int vfs_mkdir(struct fs_node *dir, const char *name)
{
    if (dir->type != FS_DIR)
        return -ENOTDIR;

    return dir->fs->mkdir(dir, name);
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

static int vfs_ioctl(struct fs_node *file, unsigned long request, void *argp)
{
    if (!file || !file->fs || !file->fs->ioctl)
        return -1;
    return file->fs->ioctl(file, request, argp);
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

struct vfs vfs = (struct vfs) {
    .init       = vfs_init,
    .install    = vfs_install,
    .mount_root = vfs_mount_root,
    .create     = vfs_create,
    .mkdir      = vfs_mkdir,
    .bind       = vfs_bind,
    .read       = vfs_read,
    .write      = vfs_write,
    .ioctl      = vfs_ioctl,
    .find       = vfs_find,
    .traverse   = vfs_traverse,
    .mount      = vfs_mount,
};
