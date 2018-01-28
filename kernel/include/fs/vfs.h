#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>
#include <bits/dirent.h>

struct fs;  /* File System Structure */
struct fs_node;
struct file;

enum fs_node_type
{
    FS_FILE, 
    FS_DIR, 
    FS_CHRDEV, 
    FS_BLKDEV, 
    FS_SYMLINK, 
    FS_PIPE,
    FS_FIFO,
    FS_SOCKET,
};

typedef struct dentry dentry_t;

struct file_ops
{
    int         (*open) (struct file *file);
    ssize_t     (*read) (struct file *file, void *buf, size_t size);    
    ssize_t     (*write)(struct file *file, void *buf, size_t size);
    ssize_t     (*readdir) (struct file *file, struct dirent *dirent);  
    ssize_t     (*close)(struct file *file);

    /* helpers */
    int         (*can_read) (struct file * file, size_t size);
    int         (*can_write)(struct file * file, size_t size);
    int         (*eof)(struct file *);
} __packed;

struct vfs_path {
    struct fs_node *mountpoint;
    char **tokens;
};

#include <dev/dev.h>
#include <sys/proc.h>
#include <ds/queue.h>

struct fs
{
    /* filesystem name */
    char * name;

    /* initalize filesystem */
    int (*init)();

    /* load filesystem from file/dev */
    struct fs_node * (*load) (struct fs_node *node);

    /* Mount filesystem */
    int (*mount) (const char *dir, int flags, void *data);

    /* create a new file in directory */
    int (*create) (struct fs_node *dir, const char *fn);

    /* create a new directory in directory */
    int (*mkdir) (struct fs_node *dir, const char *dname);

    /* kernel-level read */
    ssize_t (*read) (struct fs_node *node, off_t offset, size_t size, void *buf);

    /* kernel-level write */
    ssize_t (*write) (struct fs_node *node, off_t offset, size_t size, void *buf);

    /* kernel-level ioctl */
    int (*ioctl) (struct fs_node *node, int request, void *argp);

    /* kernel-level readdir */
    ssize_t (*readdir) (struct fs_node *node, off_t offset, struct dirent *dirent);

    /* find file/directory in directory */
    struct fs_node *(*find) (struct fs_node *dir, const char *name);

    /* Traverse path */
    struct fs_node *(*traverse) (struct vfs_path *path);

    /* File operations */
    struct file_ops f_ops;
};

struct fs_node
{
    char        *name;
    size_t      size;
    enum fs_node_type   type;
    struct fs   *fs;
    dev_t       *dev;
    off_t       offset; /* Offset to add to each operation on node */
    void        *p;     /* Filesystem handler private data */

    uint32_t    mask;   /* File access mask */
    uint32_t    uid;    /* User ID */
    uint32_t    gid;    /* Group ID */

    size_t      ref;    /* Number of processes referencing this node */
    queue_t     *read_queue;
    queue_t     *write_queue;
};

struct file
{
    struct fs_node *node;
    off_t offset;
    int flags;
};


struct vfs
{
    void    (*init) (void);
    void    (*install) (struct fs *fs);
    void    (*mount_root) (struct fs_node *inode);
    int     (*create)(struct fs_node *dir, const char *name);
    int     (*mkdir) (struct fs_node *dir, const char *name);
    int     (*open)(struct fs_node *file, int flags);
    ssize_t (*read) (struct fs_node *inode, size_t offset, size_t size, void *buf);
    ssize_t (*write)(struct fs_node *inode, size_t offset, size_t size, void *buf);
    int     (*ioctl)(struct fs_node *inode, unsigned long request, void *argp);
    int     (*bind)(const char *path, struct fs_node *target);
    int     (*mount)(const char *type, const char *dir, int flags, void *data);
    ssize_t (*readdir)(struct fs_node *inode, off_t offset, struct dirent *dirent);

    struct fs_node* (*find) (const char *name);
    struct fs_node* (*traverse) (struct vfs_path *path);
};

typedef struct 
{
    struct fs_node *node;
    enum   fs_node_type type;
    void   *p;
} vfs_mountpoint_t;


extern struct vfs vfs;
extern struct fs_node *vfs_root;

/* kernel/fs/vfs.c */
int generic_file_open(struct file *file);

/* kernel/fs/read.c */
ssize_t generic_file_read(struct file *file, void *buf, size_t size);

/* kernel/fs/write.c */
ssize_t generic_file_write(struct file *file, void *buf, size_t size);

/* kernel/fs/readdir.c */
ssize_t generic_file_readdir(struct file *file, struct dirent *dirnet);

static inline int __eof_always(struct file *f __unused){return 1;}
static inline int __eof_never (struct file *f __unused){return 0;}
static inline int __can_always(struct file *f __unused, size_t s __unused){return 1;}
static inline int __can_never (struct file *f __unused, size_t s __unused){return 0;}

#endif /* !_VFS_H */
