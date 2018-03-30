#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>
#include <bits/dirent.h>
#include <bits/errno.h>
#include <sys/proc.h>
#include <ds/queue.h>
#include <fs/stat.h>

struct fs;  /* File System Structure */
struct inode;
struct vnode;
struct file;

#define FS_RGL      1
#define FS_DIR      2
#define FS_CHRDEV   3
#define FS_BLKDEV   4
#define FS_SYMLINK  5
#define FS_PIPE     6
#define FS_FIFO     7
#define FS_SOCKET   8
#define FS_SPECIAL  9

struct uio {    /* User I/O operation */
    char     *root; /* Root Directory */
    char     *cwd;  /* Current Working Directory */
    uint32_t uid;
    uint32_t gid;
    uint32_t mask;
};

/* Inode Operations */
struct iops {
    ssize_t (*read)    (struct inode *node, off_t offset, size_t size, void *buf);
    ssize_t (*write)   (struct inode *node, off_t offset, size_t size, void *buf);
    int     (*ioctl)   (struct inode *node, int request, void *argp);
    ssize_t (*readdir) (struct inode *node, off_t offset, struct dirent *dirent);
    int     (*close)   (struct inode *node);

    int     (*vmknod)  (struct vnode *dir, const char *fn, itype_t type, dev_t dev, struct uio *uio, struct inode **ref);
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

    dev_t       dev;
    dev_t       rdev;

    void        *p;     /* Filesystem handler private data */

    uint32_t    uid;    /* User ID */
    uint32_t    gid;    /* Group ID */
    uint32_t    mask;   /* File access mask */

    ssize_t     ref;    /* Number of processes referencing this node */
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

typedef struct  {
    struct inode *node;
    uint32_t type;
    void   *p;
} vfs_mountpoint_t;


extern struct inode *vfs_root;

static inline int __vfs_always(){return 1;}
static inline int __vfs_never (){return 0;}
static inline int __vfs_nosys (){return -ENOSYS;}

/* Filesystem operations */
void    vfs_init(void);
void    vfs_install(struct fs *fs);
void    vfs_mount_root(struct inode *inode);
int     vfs_bind(const char *path, struct inode *target);
int     vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio);

/* inode operations mappings */
int     vfs_vmknod(struct vnode *dir, const char *fn, itype_t type, dev_t dev, struct uio *uio, struct inode **ref);
int     vfs_vcreat(struct vnode *dir, const char *fn, struct uio *uio, struct inode **ref);
int     vfs_vmkdir(struct vnode *dir, const char *dname, struct uio *uio, struct inode **ref);
int     vfs_vfind(struct vnode *vnode, const char *name, struct vnode *child);
int     vfs_vget(struct vnode *vnode, struct inode **inode);

ssize_t vfs_read(struct inode *inode, size_t offset, size_t size, void *buf);
ssize_t vfs_write(struct inode *inode, size_t offset, size_t size, void *buf);
ssize_t vfs_readdir(struct inode *inode, off_t offset, struct dirent *dirent);
int     vfs_ioctl(struct inode *inode, unsigned long request, void *argp);
int     vfs_close(struct inode *node);

/* file operations mappings */
int     vfs_file_open(struct file *file);
ssize_t vfs_file_read(struct file *file, void *buf, size_t size);    
ssize_t vfs_file_write(struct file *file, void *buf, size_t size);
ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent);  
off_t   vfs_file_lseek(struct file *file, off_t offset, int whence);
ssize_t vfs_file_close(struct file *file);
int     vfs_file_ioctl(struct file *file, int request, void *argp);

/* helpers */
int     vfs_file_can_read(struct file * file, size_t size);
int     vfs_file_can_write(struct file * file, size_t size);
int     vfs_file_eof(struct file *);

/* Path resolution and lookup */
int     vfs_relative(const char * const rel, const char * const path, char **abs_path);
int     vfs_lookup(const char *path, struct uio *uio, struct vnode *vnode, char **abs_path);

/* Higher level functions */
int     vfs_creat(const char *path, struct uio *uio, struct inode **ref);
int     vfs_mkdir(const char *path, struct uio *uio, struct inode **ref);
int     vfs_mknod(const char *path, itype_t type, dev_t dev, struct uio *uio, struct inode **ref);
int     vfs_stat(struct inode *inode, struct stat *buf);

#endif /* !_VFS_H */
