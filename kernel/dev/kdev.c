/**
 * \defgroup kdev kernel/kdev
 * \brief devices management
 */

#include <core/system.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <dev/blkcache.h>

MALLOC_DEFINE(M_KDEV_BLK, "kdev-blk", "kdev block buffer");

struct dev *chrdev[256];
struct dev *blkdev[256];

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

/* the file operations of a device, NULL if there is none */
struct fops *kdev_fops(struct devid *dd)
{
    struct dev *dev = kdev_get(dd);
    return dev ? &dev->fops : NULL;
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
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            if ((err = dev->bread(dd, offset/bs, 1, bbuf)) < 0)
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
        if ((err = dev->bread(dd, offset/bs, count, cbuf)) < 0)
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
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        if ((err = dev->bread(dd, offset/bs, 1, bbuf)) < 0)
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

    ssize_t ret = 0, err = 0;
    char *cbuf = buf;
    char *bbuf = NULL;

    if (offset % bs) {
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        /* Write up to block boundary */
        size_t start = MIN(bs - offset % bs, size);

        if (start) {
            if ((err = dev->bread(dd, offset/bs, 1, bbuf)) < 0)
                goto error;

            memcpy(bbuf + (offset % bs), cbuf, start);

            if ((err = dev->bwrite(dd, offset/bs, 1, bbuf)) < 0)
                goto error;

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
        if ((err = dev->bwrite(dd, offset/bs, count, cbuf)) < 0)
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
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        if ((err = dev->bread(dd, offset/bs, 1, bbuf)) < 0)
            goto error;

        memcpy(bbuf, cbuf, end);

        if ((err = dev->bwrite(dd, offset/bs, 1, bbuf)) < 0)
            goto error;

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

/*
 * A block device's bytes through an iobuf: a page at a time through a
 * kernel buffer, kdev_bread() and kdev_bwrite() make blocks of them
 */
static ssize_t kdev_blk_io(struct devid *dd, off_t offset, struct iobuf *io, int write)
{
    char *buf = kmalloc(PAGE_SIZE, &M_KDEV_BLK, 0);

    if (!buf)
        return -ENOMEM;

    size_t size = io->resid, done = 0;
    ssize_t r = 0;

    while (done < size) {
        size_t len = MIN(size - done, PAGE_SIZE);

        if (write) {
            if ((r = iobuf_in(io, buf, len)) > 0)
                r = kdev_bwrite(dd, offset + done, r, buf);
        } else {
            if ((r = kdev_bread(dd, offset + done, len, buf)) > 0)
                r = iobuf_out(io, buf, r);
        }

        if (r <= 0)
            break;

        done += r;

        /* the device's end, or a fault */
        if ((size_t) r < len)
            break;
    }

    kfree(buf);
    return done ? (ssize_t) done : r;
}

ssize_t kdev_read(struct devid *dd, off_t offset, struct iobuf *io)
{
    struct dev *dev = NULL;
    
    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (S_ISCHR(dd->type))
        return dev->read ? dev->read(dd, offset, io) : -ENXIO;
    else
        return dev->bread ? kdev_blk_io(dd, offset, io, 0) : -ENXIO;
}

ssize_t kdev_write(struct devid *dd, off_t offset, struct iobuf *io)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (S_ISCHR(dd->type))
        return dev->write ? dev->write(dd, offset, io) : -ENXIO;
    else
        return dev->bwrite ? kdev_blk_io(dd, offset, io, 1) : -ENXIO;
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

int kdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->map)
        return -ENXIO;

    return dev->map(dd, vm_space, vm_entry);
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

ssize_t kdev_file_read(struct devid *dd, struct file *file, struct iobuf *io)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.read)
        return -ENXIO;

    return dev->fops.read(file, io);
}

ssize_t kdev_file_write(struct devid *dd, struct file *file, struct iobuf *io)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.write)
        return -ENXIO;

    return dev->fops.write(file, io);
}

off_t kdev_file_lseek(struct devid *dd, struct file *file, off_t offset, int whence)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    /* terminals and other streams can't seek */
    if (!dev->fops.lseek)
        return -ESPIPE;

    return dev->fops.lseek(file, offset, whence);
}

ssize_t kdev_file_close(struct devid *dd, struct file *file)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    /* by default, release the reference to the device's vnode */
    if (!dev->fops.close)
        return posix_file_close(file);

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

    if (!dev->fops.eof)
        return -ENXIO;

    return dev->fops.eof(file);
}

/**
 * \ingroup kdev
 * \brief register a new character device
 */
void kdev_chrdev_register(devid_t major, struct dev *dev)
{
    chrdev[major] = dev; /* XXX */
    printk("kdev: registered chrdev %d: %s\n", major, dev->name);
}

/**
 * \ingroup kdev
 * \brief register a new block device
 */
/* what was written to the block cache, and what block devices have in
 * their write caches, to the media */
int kdev_sync(void)
{
    int err = blkcache_flush();

    for (int i = 0; i < 256; ++i)
        if (blkdev[i] && blkdev[i]->sync && blkdev[i]->sync())
            err = -EIO;

    return err;
}

void kdev_blkdev_register(devid_t major, struct dev *dev)
{
    blkdev[major] = dev; /* XXX */
    printk("kdev: registered blkdev %d: %s\n", major, dev->name);
}

/**
 * \ingroup kdev
 * \brief initialize kdev subsystem
 */
void kdev_init()
{
    printk("kdev: initializing\n");
}
