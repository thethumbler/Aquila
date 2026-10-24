#ifndef _DEVICE_H
#define _DEVICE_H

#include <core/system.h>

struct devid;
struct dev;

#include <mm/vm.h>
#include <fs/vfs.h>
#include <sys/proc.h>
#include <fs/iobuf.h>

/*
 * The device layer (dev/kdev.c): devices, by their major number, block or
 * character, and the operations the rest of the kernel calls on them. See
 * docs/kernel/devices.md.
 */

/** Which device: its kind (`S_IFCHR` or `S_IFBLK`) and numbers. */
struct devid {
    mode_t  type;
    devid_t major;
    devid_t minor;
};

/** A device driver: its operations, any of which may be NULL. */
struct dev {
    char    *name;

    int     (*probe)(void);

    /* a character device's: `io->resid` bytes at `offset`, through the iobuf */
    ssize_t (*read) (struct devid *dev, off_t offset, struct iobuf *io);
    ssize_t (*write)(struct devid *dev, off_t offset, struct iobuf *io);

    /* a block device's: `count` blocks from block `lba`, in kernel memory
     * (kdev_bread() and kdev_bwrite() turn them into bytes) */
    ssize_t (*bread) (struct devid *dev, off_t lba, size_t count, void *buf);
    ssize_t (*bwrite)(struct devid *dev, off_t lba, size_t count, void *buf);

    int     (*ioctl)(struct devid *dev, int request, void *argp);
    int     (*map)  (struct devid *dev, struct vm_space *vm_space, struct vm_entry *vm_entry);

    struct fops fops;

    struct dev *(*mux)(struct devid *dev);    /* the driver for a minor number */
    size_t  (*getbs)(struct devid *dev);      /* a block device's block size */
    int     (*sync)(void);                    /* writes a block device's cached writes */
};

/* Registering devices, and the operations on them (dev/kdev.c). */
void    kdev_init(void);
void    kdev_chrdev_register(devid_t major, struct dev *dev);
void    kdev_blkdev_register(devid_t major, struct dev *dev);
int     kdev_sync(void);

ssize_t kdev_read(struct devid *dd, off_t offset, struct iobuf *io);
ssize_t kdev_write(struct devid *dd, off_t offset, struct iobuf *io);
int     kdev_ioctl(struct devid *dd, int request, void *argp);
int     kdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry);

struct fops *kdev_fops(struct devid *dd);
int     kdev_file_open(struct devid *dd, struct file *file);
ssize_t kdev_file_read(struct devid *dd, struct file *file, struct iobuf *io);
ssize_t kdev_file_write(struct devid *dd, struct file *file, struct iobuf *io);
off_t   kdev_file_lseek(struct devid *dd, struct file *file, off_t offset, int whence);
ssize_t kdev_file_close(struct devid *dd, struct file *file);
int     kdev_file_ioctl(struct devid *dd, struct file *file, int request, void *argp);
int     kdev_file_can_read(struct devid *dd, struct file * file, size_t size);
int     kdev_file_can_write(struct devid *dd, struct file * file, size_t size);
int     kdev_file_eof(struct devid *dd, struct file *file);

/* A device number from its major and minor numbers, and back; a vnode's
 * device as a devid. */
#define DEV(major, minor) ((dev_t)(((major) & 0xFF) << 8) | ((minor) & 0xFF))
#define DEV_MAJOR(dev)    ((devid_t)(((dev) >> 8) & 0xFF))
#define DEV_MINOR(dev)    ((devid_t)(((dev) >> 0) & 0xFF))
#define VNODE_DEV(vnode)  ((struct devid){.type = (vnode)->mode & S_IFMT, .major = DEV_MAJOR((vnode)->rdev), .minor = DEV_MINOR((vnode)->rdev)})

/* Some drivers that are named elsewhere. */
extern struct dev ttydev;
extern struct dev pcidev;
extern struct dev atadev;
extern struct dev fbdev;
extern struct dev rddev;

#endif
