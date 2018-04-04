#include <core/system.h>
#include <dev/dev.h>
#include <fs/vfs.h>

static struct dev *chrdev[256] = {0};
static struct dev *blkdev[256] = {0};

static inline struct dev *kdev_get(struct devid *dd)
{
    struct dev *dev = NULL;

    switch (dd->type) {
        case CHRDEV:
            dev = chrdev[dd->major];
            break;
        case BLKDEV:
            dev = blkdev[dd->major];
            break;
    }

    if (dev && dev->mux) {  /* Mulitplexed Device? */
        return dev->mux(dd);
    }

    return dev;
}

int kdev_close(struct devid *dev __unused)
{
    return 0;
}

ssize_t kdev_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = NULL;
    
    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->read)
        return -ENXIO;

    return dev->read(dd, offset, size, buf);
}

ssize_t kdev_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->write)
        return -ENXIO;

    return dev->write(dd, offset, size, buf);
}

int kdev_ioctl(struct devid *dd, int request, void *argp)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->ioctl)
        return -ENXIO;

    return dev->ioctl(dd, request, argp);
}

int kdev_file_open(struct devid *dd, struct file *file)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.open)
        return -ENXIO;

    return dev->fops.open(file);
}

ssize_t kdev_file_read(struct devid *dd, struct file *file, void *buf, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.read)
        return -ENXIO;

    return dev->fops.read(file, buf, size);
}

ssize_t kdev_file_write(struct devid *dd, struct file *file, void *buf, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.write)
        return -ENXIO;

    return dev->fops.write(file, buf, size);
}

off_t kdev_file_lseek(struct devid *dd, struct file *file, off_t offset, int whence)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.lseek)
        return -ENXIO;

    return dev->fops.lseek(file, offset, whence);
}

ssize_t kdev_file_close(struct devid *dd, struct file *file)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.close)
        return -ENXIO;

    return dev->fops.close(file);
}

int kdev_file_ioctl(struct devid *dd, struct file *file, int request, void *argp)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.ioctl)
        return -ENXIO;

    return dev->fops.ioctl(file, request, argp);
}

int kdev_file_can_read(struct devid *dd, struct file * file, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.can_read)
        return -ENXIO;

    return dev->fops.can_read(file, size);
}

int kdev_file_can_write(struct devid *dd, struct file * file, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.can_write)
        return -ENXIO;

    return dev->fops.can_write(file, size);
}

int kdev_file_eof(struct devid *dd, struct file *file)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.can_write)
        return -ENXIO;

    return dev->fops.eof(file);
}

void kdev_chrdev_register(devid_t major, struct dev *dev)
{
    chrdev[major] = dev; /* XXX */
    printk("KDev: Registered chrdev %d: %s\n", major, dev->name);
}

void kdev_blkdev_register(devid_t major, struct dev *dev)
{
    blkdev[major] = dev; /* XXX */
    printk("KDev: Registered blkdev %d: %s\n", major, dev->name);
}

void kdev_init()
{
    printk("KDev: Initalizing\n");
}
