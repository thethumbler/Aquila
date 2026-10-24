#ifndef _FS_VFS_H
#define _FS_VFS_H

#include <core/system.h>

LOGGER_DECLARE(vfs_log);

struct fs;
struct vnode;
struct file;
struct uio;
struct vops;
struct iobuf;

#include <aquila/dirent.h>

/**
 * file operations
 *
 * read and write move the bytes through an iobuf (fs/iobuf.h): its
 * resid is how many are asked for
 */
struct fops {
    int         (*open)    (struct file *file);
    ssize_t     (*read)    (struct file *file, struct iobuf *io);
    ssize_t     (*write)   (struct file *file, struct iobuf *io);
    ssize_t     (*readdir) (struct file *file, struct dirent *dirent);  
    off_t       (*lseek)   (struct file *file, off_t offset, int whence);
    int         (*close)   (struct file *file);
    int         (*ioctl)   (struct file *file, int request, void *argp);
    int         (*trunc)   (struct file *file, off_t len);

    /* helpers */
    int         (*can_read)  (struct file *file, size_t size);
    int         (*can_write) (struct file *file, size_t size);
    int         (*eof)       (struct file *);

    /* how long a read with nothing to return waits (ns), 0: until data */
    uint64_t    (*read_timeout) (struct file *);
};

#include <aquila/errno.h>
#include <sys/proc.h>
#include <ds/queue.h>
#include <fs/stat.h>
#include <mm/vm.h>

MALLOC_DECLARE(M_VNODE);

/**
 * user I/O operation
 */
struct uio {
    struct vnode *root; /* where absolute paths start, NULL: vfs_root */
    struct vnode *cwd;  /* where relative ones do, NULL: the root */
    uid_t    uid;
    gid_t    gid;
    mode_t   mask;
    uint32_t flags;
};

/**
 * vnode operations
 */
struct vops {
    /* io->resid bytes at offset, through the iobuf */
    ssize_t (*read)    (struct vnode *vnode, off_t offset, struct iobuf *io);
    ssize_t (*write)   (struct vnode *vnode, off_t offset, struct iobuf *io);
    int     (*ioctl)   (struct vnode *vnode, int request, void *argp);
    int     (*close)   (struct vnode *vnode);
    int     (*trunc)   (struct vnode *vnode, off_t len);

    ssize_t (*readdir) (struct vnode *dir, off_t offset, struct dirent *dirent);
    int     (*finddir) (struct vnode *dir, const char *name, struct dirent *dirent);

    int     (*vmknod)  (struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
    int     (*vunlink) (struct vnode *dir, const char *fn, struct uio *uio);
    int     (*vlink)   (struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio);
    int     (*vrename) (struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio);

    /* store the vnode's mode, owner and times after they were changed */
    int     (*setattr) (struct vnode *vnode);

    int     (*vget)    (struct vnode *super, ino_t ino, struct vnode **vnode);

    int     (*vsync)   (struct vnode *vnode, int mode);
    int     (*sync)    (struct vnode *super, int mode);

    int     (*map)     (struct vm_space *vm_space, struct vm_entry *vm_entry);
};

/**
 * filesystem structure
 */
struct fs {
    char *name;
    int (*init)  ();
    int (*load)  (struct vnode *dev, struct vnode **super);  /* Read superblock */
    int (*mount) (struct vnode *dir, int flags, void *data);    /* on dir: vfs_bind() */

    struct vops vops;
    struct fops fops;

    /* flags */
    int nodev;
};

/**
 * in-core inode structure (vnode)
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

    struct waitq *read_queue;
    struct waitq *write_queue;

    /** virtual memory object associated with vnode */
    struct vm_object *vm_object;

    /** the pipe of a FIFO while it is open */
    struct pipe *fifo;

    /** a directory a filesystem is mounted on: that filesystem's root;
     *  a mounted filesystem's root: the directory it's mounted on */
    struct vnode *mounted;
    struct vnode *covered;
};

/**
 * An open file (POSIX's open file description, fs/file.c): one per open(),
 * pipe(), socket() or accept(), shared by the descriptors dup() and fork()
 * copy, with its offset and status flags
 */
struct file {
    union {
        struct vnode *vnode;
        struct socket *socket;
    };

    off_t offset;
    int flags;      /* open flags (O_*) */

    /* its flock() lock */
    struct file_lock *lock;

    /* the descriptors, and the system calls, referring to it */
    int ref;
};

/* a new open file, its one reference the caller's */
struct file *file_new(void);

/* one more reference */
struct file *file_ref(struct file *file);

/* a reference dropped: the last one closes and frees the file (the
 * close's result) */
int file_put(struct file *file);

/* held by the current thread's system call until it returns */
struct file *file_hold(struct file *file);

/**
 * list of registered filesystems
 */
struct fs_list {
    /** filesystem name */
    const char *name;

    /** filesystem structure */
    struct fs  *fs;

    /** next entry in the list */
    struct fs_list *next;
};

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
int     vfs_mount_root_dev(const char *name, const char *type);
int     vfs_bind(struct vnode *dir, struct vnode *root);
int     vfs_mount(const char *type, const char *dir, int flags, void *data, struct uio *uio);

/* vnode operations mappings */
int     vfs_vmknod(struct vnode *dir, const char *fn, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int     vfs_vcreat(struct vnode *dir, const char *fn, struct uio *uio, struct vnode **ref);
int     vfs_vmkdir(struct vnode *dir, const char *dname, struct uio *uio, struct vnode **ref);
int     vfs_vunlink(struct vnode *dir, const char *fn, struct uio *uio);
int     vfs_vlink(struct vnode *dir, const char *fn, struct vnode *vnode, struct uio *uio);
int     vfs_vrename(struct vnode *odir, const char *ofn, struct vnode *ndir, const char *nfn, struct uio *uio);
int     vfs_setattr(struct vnode *vnode);
int     vfs_vget(struct vnode *super, ino_t ino, struct vnode **vnode);

int     vfs_map(struct vm_space *vm_space, struct vm_entry *vm_entry);

ssize_t vfs_read_io(struct vnode *vnode, off_t offset, struct iobuf *io);
ssize_t vfs_write_io(struct vnode *vnode, off_t offset, struct iobuf *io);

/* the kernel's own: a kernel buffer */
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
int     vfs_dir_empty(struct vnode *dir);

/* file operations mappings */
int     vfs_file_open(struct file *file);
ssize_t vfs_file_read(struct file *file, struct iobuf *io);
ssize_t vfs_file_write(struct file *file, struct iobuf *io);
ssize_t vfs_file_readdir(struct file *file, struct dirent *dirent);  
off_t   vfs_file_lseek(struct file *file, off_t offset, int whence);
ssize_t vfs_file_close(struct file *file);
int     vfs_file_flock(struct file *file, int op);
void    vfs_file_lock_release(struct file *file);
int     vfs_file_ioctl(struct file *file, int request, void *argp);
int     vfs_file_trunc(struct file *file, off_t len);

/* helpers */
int     vfs_file_can_read(struct file * file, size_t size);
struct waitq *vfs_file_wait_queue(struct file *file, int write);
int     vfs_file_can_write(struct file * file, size_t size);
int     vfs_file_eof(struct file *);

/* Path lookup (fs/lookup.c) */
int     vfs_lookup(const char *path, struct uio *uio, struct vnode **vnode);
int     vfs_lookup_parent(const char *path, struct uio *uio, struct vnode **dir, char **name);
int     vfs_lookup_dir(const char *path, struct uio *uio, struct vnode **vnode, struct vnode **dir);
int     vfs_parent(struct vnode *dir, struct vnode **parent);
struct vnode *vfs_mount_root_of(struct vnode *vnode);
int     vfs_under(struct vnode *vnode, struct vnode *dir);
int     vfs_vnode_path(struct vnode *vnode, struct vnode *root, char *buf, size_t size);

/* Higher level functions */
int     vfs_creat(const char *path, mode_t mode, struct uio *uio, struct vnode **ref);
int     vfs_mkdir(const char *path, mode_t mode, struct uio *uio, struct vnode **ref);
int     vfs_mknod(const char *path, uint32_t mode, dev_t dev, struct uio *uio, struct vnode **ref);
int     vfs_unlink(const char *path, struct uio *uio);
int     vfs_rmdir(const char *path, struct uio *uio);
int     vfs_link(const char *oldpath, struct uio *ouio, const char *newpath, struct uio *nuio);
int     vfs_symlink(const char *target, const char *path, struct uio *uio);
ssize_t vfs_readlink(const char *path, struct uio *uio, char *buf, size_t size);
int     vfs_rename(const char *oldpath, struct uio *ouio, const char *newpath, struct uio *nuio);
int     vfs_stat(struct vnode *vnode, struct aquila_stat *buf);
int     vfs_perms_check(struct file *file, struct uio *uio);

/* access modes for vfs_access() */
#define VFS_R   4
#define VFS_W   2
#define VFS_X   1
int     vfs_access(struct vnode *vnode, struct uio *uio, int mode);

/* changing attributes, `times' is atime then mtime (UTIME_NOW/UTIME_OMIT) */
int     vfs_chmod(struct vnode *vnode, mode_t mode, struct uio *uio);
int     vfs_chown(struct vnode *vnode, uid_t uid, gid_t gid, struct uio *uio);
int     vfs_utimens(struct vnode *vnode, const struct aquila_timespec times[2], struct uio *uio);

/* XXX */

struct mountpoint {
    const char *dev;
    const char *path;
    const char *type;
    const char *options;
};

#endif /* ! _FS_VFS_H */
