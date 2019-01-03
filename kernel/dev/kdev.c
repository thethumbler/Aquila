#include <core/system.h>
#include <dev/dev.h>
#include <fs/vfs.h>

static struct dev *chrdev[256] = {0};
static struct dev *blkdev[256] = {0};

static inline struct dev *kdev_get(struct devid *dd)
{
    struct dev *dev = NULL;

    switch (dd->type) {
        case S_IFCHR:
            dev = chrdev[dd->major];
            break;
        case S_IFBLK:
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

ssize_t kdev_bread(struct devid *dd, off_t offset, size_t size, void *buf)
{
    //printk("kdev_bread(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);

    struct dev *dev = kdev_get(dd);
    size_t bs = dev->getbs(dd);

    int err = 0;
    ssize_t ret = 0;

    char *cbuf = buf;
    char *bbuf = NULL;

    /* Read up to block boundary */
    if (offset % bs) {
        if (!bbuf && !(bbuf = kmalloc(bs)))
            return -ENOMEM;

        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            if ((err = dev->read(dd, offset/bs, 1, bbuf)) < 0)
                goto error;

            memcpy(cbuf, bbuf + (offset % bs), start);

            ret    += start;
            size   -= start;
            cbuf   += start;
            offset += start;

            if (!size)
                goto done;
        }
    }

    /* Read entire blocks */
    size_t count = size/bs;

    if (count) {
        if ((err = dev->read(dd, offset/bs, count, cbuf)) < 0)
            goto error;

        ret    += count * bs;
        size   -= count * bs;
        cbuf   += count * bs;
        offset += count * bs;

        if (!size)
            goto done;
    }

    size_t end = size % bs;

    if (end) {
        if (!bbuf && !(bbuf = kmalloc(bs)))
            return -ENOMEM;

        if ((err = dev->read(dd, offset/bs, 1, bbuf)) < 0)
            goto error;

        memcpy(cbuf, bbuf, end);
        ret += end;
    }

done:
    if (bbuf)
        kfree(bbuf);

    return ret;

error:
    if (bbuf)
        kfree(bbuf);

    return err;
}

ssize_t kdev_bwrite(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = kdev_get(dd);
    size_t bs = dev->getbs(dd);

    ssize_t ret = 0;
    char *cbuf = buf;
    char *bbuf = NULL;

    if (offset % bs) {
        if (!bbuf && !(bbuf = kmalloc(bs)))
            return -ENOMEM;

        /* Write up to block boundary */
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            dev->read(dd, offset/bs, 1, bbuf);
            memcpy(bbuf + (offset % bs), cbuf, start);
            dev->write(dd, offset/bs, 1, bbuf);

            ret    += start;
            size   -= start;
            cbuf   += start;
            offset += start;

            if (!size)
                goto done;
        }
    }


    /* Write entire blocks */
    size_t count = size/bs;

    if (count) {
        dev->write(dd, offset/bs, count, cbuf);

        ret    += count * bs;
        size   -= count * bs;
        cbuf   += count * bs;
        offset += count * bs;

        if (!size)
            goto done;
    }

    size_t end = size % bs;

    if (end) {
        if (!bbuf && !(bbuf = kmalloc(bs)))
            return -ENOMEM;

        dev->read(dd, offset/bs, 1, bbuf);
        memcpy(bbuf, cbuf, end);
        dev->write(dd, offset/bs, 1, bbuf);
        ret += end;
    }

done:
    if (bbuf)
        kfree(bbuf);

    return ret;
}

ssize_t kdev_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    //printk("kdev_read(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);

    struct dev *dev = NULL;
    
    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->read)
        return -ENXIO;

    if (S_ISCHR(dd->type))
        return dev->read(dd, offset, size, buf);
    else
        return kdev_bread(dd, offset, size, buf);
}

ssize_t kdev_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->write)
        return -ENXIO;

    if (S_ISCHR(dd->type))
        return dev->write(dd, offset, size, buf);
    else
        return kdev_bwrite(dd, offset, size, buf);
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

int kdev_mmap(struct devid *dd, struct vmr *vmr)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->mmap)
        return -ENXIO;

    return dev->mmap(dd, vmr);
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
    printk("kdev: Registered chrdev %d: %s\n", major, dev->name);
}

void kdev_blkdev_register(devid_t major, struct dev *dev)
{
    blkdev[major] = dev; /* XXX */
    printk("kdev: Registered blkdev %d: %s\n", major, dev->name);
}

void kdev_init()
{
    printk("kdev: Initializing\n");
}
