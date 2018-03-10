#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>
#include <bits/dirent.h>
#include <bits/errno.h>

struct fs;  /* File System Structure */
struct inode;
struct vnode;
struct file;

typedef uintptr_t vino_t;    /* vino_t should be large enough to hold a pointer */

#define FS_RGL      1
#define FS_DIR      2
#define FS_CHRDEV   3
#define FS_BLKDEV   4
#define FS_SYMLINK  5
#define FS_PIPE     6
#define FS_FIFO     7
#define FS_SOCKET   8
#define FS_SPECIAL  9

typedef struct dentry dentry_t;

/* Inode Operations */
struct iops {
    int     (*create)  (struct vnode *dir, const char *fn, int uid, int gid, int mode, struct inode **node);
    int     (*mkdir)   (struct vnode *dir, const char *dname, int uid, int gid, int mode);
    ssize_t (*read)    (struct inode *node, off_t offset, size_t size, void *buf);
    ssize_t (*write)   (struct inode *node, off_t offset, size_t size, void *buf);
    int     (*ioctl)   (struct inode *node, int request, void *argp);
    ssize_t (*readdir) (struct inode *node, off_t offset, struct dirent *dirent);

    int     (*vfind)   (struct vnode *dir, const char *name, struct vnode *child);
    int     (*vget)    (struct vnode *vnode, struct inode **inode);
} __packed;

/* File Operations */
struct fops {
    int         (*open)    (struct file *file);
    ssize_t     (*read)    (struct file *file, void *buf, size_t size);    
    ssize_t     (*write)   (struct file *file, void *buf, size_t size);
    ssize_t     (*readdir) (struct file *file, struct dirent *dirent);  
    off_t       (*lseek)   (struct file *file, off_t offset, int whence);
    ssize_t     (*close)   (struct file *file);
    int         (*ioctl)   (struct file *file, int request, void *argp);

    /* helpers */
    int         (*can_read)  (struct file * file, size_t size);
    int         (*can_write) (struct file * file, size_t size);
    int         (*eof)       (struct file *);
} __packed;

struct vfs_path {
    struct inode *mountpoint;
    char **tokens;
};

#include <dev/dev.h>
#include <sys/proc.h>
#include <ds/queue.h>

struct fs {
    char *name;
    int (*init)  ();
    int (*load)  (struct inode *dev, struct inode **super);  /* Read superblock */
    int (*mount) (const char *dir, int flags, void *data);

    struct iops iops;
    struct fops fops;
};

struct inode {    /* Actual inode, only one copy is permitted */
    vino_t      id;     /* Unique Identifier */
    char        *name;
    size_t      size;
    uint32_t    type;
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

struct vnode {  /* Tag node, multiple copies are permitted */
    struct inode *super;  /* super node containing inode reference */
    vino_t       id;     /* unique underlying inode identifier */
    uint32_t     type;   /* inode type */
    uint32_t     mask;   /* inode access mask */
    uint32_t     uid;    /* user ID */
    uint32_t     gid;    /* group ID */
};

struct file {
    struct inode *node;
    off_t offset;
    int flags;
};

struct vfs {
    /* Filesystem operations */
    void    (*init)       (void);
    void    (*install)    (struct fs *fs);
    void    (*mount_root) (struct inode *inode);
    int     (*bind)       (const char *path, struct inode *target);
    int     (*mount)      (const char *type, const char *dir, int flags, void *data);

    /* inode operations mappings */
    int     (*create)  (struct vnode *dir, const char *fn, int uid, int gid, int mode, struct inode **node);
    int     (*mkdir)   (struct vnode *dir, const char *dname, int uid, int gid, int mode);
    ssize_t (*read)    (struct inode *inode, size_t offset, size_t size, void *buf);
    ssize_t (*write)   (struct inode *inode, size_t offset, size_t size, void *buf);
    ssize_t (*readdir) (struct inode *inode, off_t offset, struct dirent *dirent);
    int     (*ioctl)   (struct inode *inode, unsigned long request, void *argp);

    /* file operations mappings */
    struct  fops fops;

    /* Path resolution and lookup */
    int     (*relative) (const char * const rel, const char * const path, char **abs_path);
    int     (*lookup)   (const char *path, uint32_t uid, uint32_t gid, struct vnode *vnode);
    int     (*vfind)    (struct vnode *vnode, const char *name, struct vnode *child);
    int     (*vget)     (struct vnode *vnode, struct inode **inode);
};

typedef struct  {
    struct inode *node;
    uint32_t type;
    void   *p;
} vfs_mountpoint_t;


extern struct vfs vfs;
extern struct inode *vfs_root;

/* kernel/fs/vfs.c */
int generic_file_open(struct file *file);

static inline int __vfs_always(){return 1;}
static inline int __vfs_never (){return 0;}
static inline int __vfs_nosys (){return -ENOSYS;}

#endif /* !_VFS_H */
