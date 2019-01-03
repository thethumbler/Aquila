#ifndef _VFS_H
#define _VFS_H

#include <core/system.h>

struct fs;
struct inode;
struct vnode;
struct file;
struct uio;
struct iops;

#include <bits/dirent.h>

struct fops {
    int         (*open)    (struct file *file);
    ssize_t     (*read)    (struct file *file, void *buf, size_t size);    
    ssize_t     (*write)   (struct file *file, void *buf, size_t size);
    ssize_t     (*readdir) (struct file *file, struct dirent *dirent);  
    off_t       (*lseek)   (struct file *file, off_t offset, int whence);
    int         (*close)   (struct file *file);
    int         (*ioctl)   (struct file *file, int request, void *argp);

    /* helpers */
    int         (*can_read)  (struct file *file, size_t size);
    int         (*can_write) (struct file *file, size_t size);
    int         (*eof)       (struct file *);
};

#include <bits/errno.h>
#include <sys/proc.h>
#include <ds/queue.h>

#include <fs/stat.h>

#include <mm/vm.h>

/* User I/O operation */
struct uio {
    char     *root; /* Root Directory */
    char     *cwd;  /* Current Working Directory */
    uid_t    uid;
    gid_t    gid;
    mode_t   mask;
    uint32_t flags;
};

/* Inode Operations */
struct iops {
    ssize_t (*read)    (struct inode *inode, off_t offset, size_t size, void *buf);
    ssize_t (*write)   (struct inode *inode, off_t offset, size_t size, void *buf);
    int     (*ioctl)   (struct inode *inode, int request, void *argp);
    ssize_t (*readdir) (struct inode *inode, off_t offset, struct dirent *dirent);
    int     (*close)   (struct inode *inode);

    int     (*vmknod)  (struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct inode **ref);
    int     (*vunlink) (struct vnode *dir, const char *fn, struct uio *uio);
    int     (*vfind)   (struct vnode *dir, const char *name, struct vnode *child);
    int     (*vget)    (struct vnode *vnode, struct inode **inode);
    int     (*mmap)    (struct vmr *vmr);
};

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

    /* flags */
    int nodev : 1;
};

/* in-core inode structure */
struct inode {
    /* fields common to all filesystems */
    ino_t       ino;
    size_t      size;
    dev_t       dev;
    dev_t       rdev;
    mode_t      mode;
    uid_t       uid;
    gid_t       gid;
    nlink_t     nlink;
    _time_t     atime;
    _time_t     mtime;
    _time_t     ctime;


    //char        *name;
    struct fs   *fs;

    void        *p;     /* Filesystem handler private data */
    ssize_t     ref;    /* Number of processes referencing this node */

    struct queue     *read_queue;
    struct queue     *write_queue;
};

struct vnode {
    vino_t ino;
    mode_t mode;
    uid_t  uid;
    gid_t  gid;

    /* super node containing inode reference */
    struct inode *super;  
};

struct file {
    union {
        struct inode *inode;
        struct socket *socket;
    };

    off_t offset;
    int flags;
    //int ref;
};

typedef struct  {
    struct inode *node;
    uint32_t type;
    void   *p;
} vfs_mountpoint_t;

struct fs_list {
    const char *name;
    struct fs  *fs;
    struct fs_list *next;
};

/* XXX */
struct vfs_path *vfs_get_mountpoint(char **tokens);
char **canonicalize_path(const char * const path);
int vfs_parse_path(const char *path, struct uio *uio, char **abs_path);

extern struct fs_list *registered_fs;
extern struct inode *vfs_root;

static inline int __vfs_always(){return 1;}
static inline int __vfs_never (){return 0;}
static inline int __vfs_nosys (){return -ENOSYS;}
static inline int __vfs_rofs  (){return -EROFS;}

#define __VMKNOD_T (int (*)(struct vnode *, const char *, uint32_t, dev_t, struct uio *, struct inode **))

/* Filesystem operations */
void    vfs_init(void);
void    vfs_install(struct fs *fs);
void    vfs_mount_root(struct inode *inode);
int     vfs_bind(const char *path, struct inode *target);
int     vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio);

/* inode operations mappings */
int     vfs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct inode **ref);
int     vfs_vcreat(struct vnode *dir, const char *fn, struct uio *uio, struct inode **ref);
int     vfs_vmkdir(struct vnode *dir, const char *dname, struct uio *uio, struct inode **ref);
int     vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int     vfs_vfind(struct vnode *vnode, const char *name, struct vnode *child);
int     vfs_vget(struct vnode *vnode, struct inode **inode);
int     vfs_mmap(struct vmr *vmr);

ssize_t vfs_read(struct inode *inode, off_t offset, size_t size, void *buf);
ssize_t vfs_write(struct inode *inode, off_t offset, size_t size, void *buf);
ssize_t vfs_readdir(struct inode *inode, off_t offset, struct dirent *dirent);
int     vfs_ioctl(struct inode *inode, unsigned long request, void *argp);
int     vfs_close(struct inode *inode);

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
int     vfs_creat(const char *path, mode_t mode, struct uio *uio, struct inode **ref);
int     vfs_mkdir(const char *path, mode_t mode, struct uio *uio, struct inode **ref);
int     vfs_mknod(const char *path, uint32_t mode, dev_t dev, struct uio *uio, struct inode **ref);
int     vfs_unlink(const char *path, struct uio *uio);
int     vfs_stat(struct inode *inode, struct stat *buf);
int     vfs_perms_check(struct file *file, struct uio *uio);

#endif /* !_VFS_H */
