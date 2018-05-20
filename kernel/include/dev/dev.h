#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>
#include <fs/vfs.h>
#include <sys/proc.h>

enum {
    CHRDEV = FS_CHRDEV,
    BLKDEV = FS_BLKDEV,
};

struct devid {
    int     type;
    devid_t major;
    devid_t minor;
};

struct dev {
    char    *name;

    int     (*probe)();
    ssize_t (*read) (struct devid *dev, off_t offset, size_t size, void *buf);
    ssize_t (*write)(struct devid *dev, off_t offset, size_t size, void *buf);
    int     (*ioctl)(struct devid *dev, int request, void *argp);
    int     (*mmap)(struct devid *dev, struct vmr *vmr);

    struct fops fops;

    struct dev *(*mux)(struct devid *dev);    /* Device Multiplexr */
    size_t  (*getbs)(struct devid *dev);      /* Block size, for blkdev */
} __packed;

/* Kernel Device Subsystem Handlers */
void    kdev_init();
void    kdev_chrdev_register(devid_t major, struct dev *dev);
void    kdev_blkdev_register(devid_t major, struct dev *dev);

ssize_t kdev_read(struct devid *dd, off_t offset, size_t size, void *buf);
ssize_t kdev_write(struct devid *dd, off_t offset, size_t size, void *buf);
int     kdev_ioctl(struct devid *dd, int request, void *argp);
int     kdev_mmap(struct devid *dd, struct vmr *vmr);

int     kdev_file_open(struct devid *dd, struct file *file);
ssize_t kdev_file_read(struct devid *dd, struct file *file, void *buf, size_t size);    
ssize_t kdev_file_write(struct devid *dd, struct file *file, void *buf, size_t size);
off_t   kdev_file_lseek(struct devid *dd, struct file *file, off_t offset, int whence);
ssize_t kdev_file_close(struct devid *dd, struct file *file);
int     kdev_file_ioctl(struct devid *dd, struct file *file, int request, void *argp);
int     kdev_file_can_read(struct devid *dd, struct file * file, size_t size);
int     kdev_file_can_write(struct devid *dd, struct file * file, size_t size);
int     kdev_file_eof(struct devid *dd, struct file *file);

/* Useful Macros */
#define _DEV_T(major, minor) ((dev_t)(((major) & 0xFF) << 8) | ((minor) & 0xFF))
#define _DEV_MAJOR(dev) ((devid_t)(((dev) >> 8) & 0xFF))
#define _DEV_MINOR(dev) ((devid_t)(((dev) >> 0) & 0xFF))
#define _INODE_DEV(inode) ((struct devid){.type = (inode)->type, .major = _DEV_MAJOR((inode)->rdev), .minor = _DEV_MINOR((inode)->rdev)})

/* Devices -- XXX */
extern struct dev i8042dev;
extern struct dev ps2kbddev;
extern struct dev condev;
extern struct dev ttydev;
extern struct dev pcidev;
extern struct dev atadev;
extern struct dev fbdev;
extern struct dev rddev;

#endif
