#ifndef _FS_VFS_H
#define _FS_VFS_H

#include <core/system.h>

LOGGER_DECLARE(vfs_log);

struct fs;
struct vnode;
struct file;
struct uio;
struct vops;

#include <bits/dirent.h>

/**
 * \ingroup vfs
 * \brief file operations
 */
struct fops {
    int         (*open)    (struct file *file);
    ssize_t     (*read)    (struct file *file, void *buf, size_t size);    
    ssize_t     (*write)   (struct file *file, void *buf, size_t size);
    ssize_t     (*readdir) (struct file *file, struct dirent *dirent);  
    off_t       (*lseek)   (struct file *file, off_t offset, int whence);
    int         (*close)   (struct file *file);
    int         (*ioctl)   (struct file *file, int request, void *argp);
    int         (*trunc)   (struct file *file, off_t len);

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

MALLOC_DECLARE(M_VNODE);

/**
 * \ingroup vfs
 * \brief user I/O operation
 */
struct uio {
    char     *root; /* Root Directory */
    char     *cwd;  /* Current Working Directory */
    uid_t    uid;
    gid_t    gid;
    mode_t   mask;
    uint32_t flags;
};

/**
 * \ingroup vfs
 * \brief vnode operations
 */
struct vops {
    ssize_t (*read)    (struct vnode *vnode, off_t offset, size_t size, void *buf);
    ssize_t (*write)   (struct vnode *vnode, off_t offset, size_t size, void *buf);
    int     (*ioctl)   (struct vnode *vnode, int request, void *argp);
    int     (*close)   (struct vnode *vnode);
    int     (*trunc)   (struct vnode *vnode, off_t len);

    ssize_t (*readdir) (struct vnode *dir, off_t offset, struct dirent *dirent);
    int     (*finddir) (struct vnode *dir, const char *name, struct dirent *dirent);

    int     (*vmknod)  (struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
    int     (*vunlink) (struct vnode *dir, const char *fn, struct uio *uio);

    int     (*vget)    (struct vnode *super, ino_t ino, struct vnode **vnode);

    int     (*vsync)   (struct vnode *vnode, int mode);
    int     (*sync)    (struct vnode *super, int mode);

    int     (*map)     (struct vm_space *vm_space, struct vm_entry *vm_entry);
};

/**
 * \ingroup vfs
 */
struct vfs_path {
    struct vnode *root;
    char **tokens;
};

/**
 * \ingroup vfs
 * \brief filesystem structure
 */
struct fs {
    char *name;
    int (*init)  ();
    int (*load)  (struct vnode *dev, struct vnode **super);  /* Read superblock */
    int (*mount) (const char *dir, int flags, void *data);

    struct vops vops;
    struct fops fops;

    /* flags */
    int nodev;
};

/**
 * \ingroup vfs
 * \brief in-core inode structure (vnode)
 */
struct vnode {
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

    struct fs   *fs;

    /** filesystem handler private data */
    void *p;

    /** number of processes referencing this vnode */
    size_t ref;

    struct queue *read_queue;
    struct queue *write_queue;

    /** virtual memory object associated with vnode */
    struct vm_object *vm_object;
};

struct file {
    union {
        struct vnode *vnode;
        struct socket *socket;
    };

    off_t offset;
    int flags;
    //int ref;
};

/**
 * \ingroup vfs
 * \brief list of registered filesystems
 */
struct fs_list {
    /** filesystem name */
    const char *name;

    /** filesystem structure */
    struct fs  *fs;

    /** next entry in the list */
    struct fs_list *next;
};

/* XXX */
struct vfs_path *vfs_get_mountpoint(char **tokens);
char **tokenize_path(const char * const path);
int vfs_parse_path(const char *path, struct uio *uio, char **abs_path);

extern struct fs_list *registered_fs;
extern struct vnode *vfs_root;

static inline int __vfs_can_always(struct file *f, size_t s){return 1;}
static inline int __vfs_can_never (struct file *f, size_t s){return 0;}
static inline int __vfs_eof_always(struct file *f){return 1;}
static inline int __vfs_eof_never (struct file *f){return 0;}

#define ISDEV(vnode) (S_ISCHR((vnode)->mode) || S_ISBLK((vnode)->mode))

/* Filesystem operations */
void    vfs_init(void);
int     vfs_install(struct fs *fs);
int     vfs_mount_root(struct vnode *vnode);
int     vfs_bind(const char *path, struct vnode *target);
int     vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio);

/* vnode operations mappings */
int     vfs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int     vfs_vcreat(struct vnode *dir, const char *fn, struct uio *uio, struct vnode **ref);
int     vfs_vmkdir(struct vnode *dir, const char *dname, struct uio *uio, struct vnode **ref);
int     vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int     vfs_vget(struct vnode *super, ino_t ino, struct vnode **vnode);

int     vfs_map(struct vm_space *vm_space, struct vm_entry *vm_entry);

ssize_t vfs_read(struct vnode *vnode, off_t offset, size_t size, void *buf);
ssize_t vfs_write(struct vnode *vnode, off_t offset, size_t size, void *buf);
int     vfs_ioctl(struct vnode *vnode, unsigned long request, void *argp);
int     vfs_close(struct vnode *vnode);
int     vfs_trunc(struct vnode *vnode, off_t len);

struct vm_page *vfs_pagein(struct vnode *vnode, off_t offset);

/* sync */
#define FS_MSYNC    0x0001
#define FS_DSYNC    0x0002
int vfs_vsync(struct vnode *vnode, int mode);
int vfs_fssync(struct vnode *super, int mode);
int vfs_sync(int mode);

ssize_t vfs_readdir(struct vnode *dir, off_t offset, struct dirent *dirent);
int     vfs_finddir(struct vnode *dir, const char *name, struct dirent *dirent);

/* file operations mappings */
int     vfs_file_open(struct file *file);
ssize_t vfs_file_read(struct file *file, void *buf, size_t size);    
ssize_t vfs_file_write(struct file *file, void *buf, size_t size);
ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent);  
off_t   vfs_file_lseek(struct file *file, off_t offset, int whence);
ssize_t vfs_file_close(struct file *file);
int     vfs_file_ioctl(struct file *file, int request, void *argp);
int     vfs_file_trunc(struct file *file, off_t len);

/* helpers */
int     vfs_file_can_read(struct file * file, size_t size);
int     vfs_file_can_write(struct file * file, size_t size);
int     vfs_file_eof(struct file *);

/* Path resolution and lookup */
int     vfs_relative(const char * const rel, const char * const path, char **abs_path);
int     vfs_lookup(const char *path, struct uio *uio, struct vnode **vnode, char **abs_path);

/* Higher level functions */
int     vfs_creat(const char *path, mode_t mode, struct uio *uio, struct vnode **ref);
int     vfs_mkdir(const char *path, mode_t mode, struct uio *uio, struct vnode **ref);
int     vfs_mknod(const char *path, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int     vfs_unlink(const char *path, struct uio *uio);
int     vfs_stat(struct vnode *vnode, struct stat *buf);
int     vfs_perms_check(struct file *file, struct uio *uio);

/* XXX */

struct mountpoint {
    const char *dev;
    const char *path;
    const char *type;
    const char *options;
};

#endif /* ! _FS_VFS_H */
