#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>

struct devid;
struct dev;

#include <mm/vm.h>
#include <fs/vfs.h>
#include <sys/proc.h>

/**
 * \ingroup kdev
 * \brief device identifier
 */
struct devid {
    mode_t  type;
    devid_t major;
    devid_t minor;
};

/**
 * \ingroup kdev
 * \brief device
 */
struct dev {
    char    *name;

    int     (*probe)(void);
    ssize_t (*read) (struct devid *dev, off_t offset, size_t size, void *buf);
    ssize_t (*write)(struct devid *dev, off_t offset, size_t size, void *buf);
    int     (*ioctl)(struct devid *dev, int request, void *argp);
    int     (*map)  (struct devid *dev, struct vm_space *vm_space, struct vm_entry *vm_entry);

    struct fops fops;

    struct dev *(*mux)(struct devid *dev);    /* Device Multiplexr */
    size_t  (*getbs)(struct devid *dev);      /* Block size, for blkdev */
};

/* Kernel Device Subsystem Handlers */
void    kdev_init(void);
void    kdev_chrdev_register(devid_t major, struct dev *dev);
void    kdev_blkdev_register(devid_t major, struct dev *dev);

ssize_t kdev_read(struct devid *dd, off_t offset, size_t size, void *buf);
ssize_t kdev_write(struct devid *dd, off_t offset, size_t size, void *buf);
int     kdev_ioctl(struct devid *dd, int request, void *argp);
int     kdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry);

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
#define DEV(major, minor) ((dev_t)(((major) & 0xFF) << 8) | ((minor) & 0xFF))
#define DEV_MAJOR(dev)    ((devid_t)(((dev) >> 8) & 0xFF))
#define DEV_MINOR(dev)    ((devid_t)(((dev) >> 0) & 0xFF))
#define VNODE_DEV(vnode)  ((struct devid){.type = (vnode)->mode & S_IFMT, .major = DEV_MAJOR((vnode)->rdev), .minor = DEV_MINOR((vnode)->rdev)})

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
