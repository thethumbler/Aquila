/** \file
 * The device layer: devices by their numbers, and what calls them.
 *
 * A driver registers itself as the character or block device of a major
 * number (`kdev_chrdev_register()`, `kdev_blkdev_register()`); a driver
 * with several devices picks one by minor with its `mux`. The rest of the
 * kernel reaches a device through these functions, by its number. Block
 * devices are read and written in whole blocks: `kdev_bread()` and
 * `kdev_bwrite()` make any range of bytes into blocks. See
 * docs/kernel/devices.md.
 */

#include <core/system.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/posix.h>
#include <dev/blkcache.h>

MALLOC_DEFINE(M_KDEV_BLK, "kdev-blk", "kdev block buffer");

struct dev *chrdev[256];
struct dev *blkdev[256];

/** Returns the driver of a device number, picked by minor if the major's
 * driver multiplexes several devices, or NULL if there is none. */
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

    if (dev && dev->mux) {  /* several devices, one per minor */
        return dev->mux(dd);
    }

    return dev;
}

/**
 * Returns the file operations of a device.
 *
 * @param dd  the device's number
 * @return its file operations, or NULL if there is no such device
 */
struct fops *kdev_fops(struct devid *dd)
{
    struct dev *dev = kdev_get(dd);
    return dev ? &dev->fops : NULL;
}

/**
 * Closes a device: there is nothing to do.
 *
 * @param dev  the device's number
 * @return 0
 */
int kdev_close(struct devid *dev __unused)
{
    return 0;
}

/**
 * Reads bytes from a block device, at any offset: the driver reads whole
 * blocks, and the partial blocks at either end go through a buffer.
 *
 * @param dd      the device's number
 * @param offset  where to read from, in bytes
 * @param size    how many bytes to read
 * @param buf     where to put them
 * @return how many bytes were read (fewer at the device's end), or a negative
 *         errno if nothing could be
 */
ssize_t kdev_bread(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = kdev_get(dd);

    if (!dev || !dev->bread || !dev->getbs)
        return -ENXIO;

    size_t bs = dev->getbs(dd);
    ssize_t ret = 0, r = 0;

    char *cbuf = buf;
    char *bbuf = NULL;

    /* a driver's bread() returns how many blocks it read: if it read fewer
     * than asked, the device ended there, and so does what is read */

    /* the part up to the first block boundary */
    if (offset % bs) {
        if (!(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        size_t start = MIN(bs - offset % bs, size);

        if ((r = dev->bread(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        memcpy(cbuf, bbuf + (offset % bs), start);

        ret    += start;
        size   -= start;
        cbuf   += start;
        offset += start;

        if (!size)
            goto done;
    }

    /* the whole blocks */
    size_t count = size/bs;

    if (count) {
        if ((r = dev->bread(dd, offset/bs, count, cbuf)) < 0)
            goto done;

        ret    += r * bs;
        size   -= r * bs;
        cbuf   += r * bs;
        offset += r * bs;

        if (!size || (size_t) r < count)
            goto done;
    }

    size_t end = size % bs;

    if (end) {
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return ret ? ret : -ENOMEM;

        if ((r = dev->bread(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        memcpy(cbuf, bbuf, end);
        ret += end;
    }

done:
    kfree(bbuf);

    /* return the error only if nothing was read before it */
    return r < 0 && !ret ? r : ret;
}

/**
 * Writes bytes to a block device, at any offset: the driver writes whole
 * blocks, and the partial blocks at either end are read, changed and
 * written back.
 *
 * @param dd      the device's number
 * @param offset  where to write, in bytes
 * @param size    how many bytes to write
 * @param buf     the bytes
 * @return how many bytes were written, or a negative errno if nothing could
 *         be (`-ENOSPC` past the device's end)
 */
ssize_t kdev_bwrite(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct dev *dev = kdev_get(dd);

    if (!dev || !dev->bread || !dev->bwrite || !dev->getbs)
        return -ENXIO;

    size_t bs = dev->getbs(dd);
    ssize_t ret = 0, r = 0;

    char *cbuf = buf;
    char *bbuf = NULL;

    /* as in kdev_bread(): fewer blocks than asked means the device ended
     * there */

    if (offset % bs) {
        if (!(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return -ENOMEM;

        /* the part up to the first block boundary */
        size_t start = MIN(bs - offset % bs, size);

        if ((r = dev->bread(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        memcpy(bbuf + (offset % bs), cbuf, start);

        if ((r = dev->bwrite(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        ret    += start;
        size   -= start;
        cbuf   += start;
        offset += start;

        if (!size)
            goto done;
    }

    /* the whole blocks */
    size_t count = size/bs;

    if (count) {
        if ((r = dev->bwrite(dd, offset/bs, count, cbuf)) < 0)
            goto done;

        ret    += r * bs;
        size   -= r * bs;
        cbuf   += r * bs;
        offset += r * bs;

        if (!size || (size_t) r < count)
            goto done;
    }

    size_t end = size % bs;

    if (end) {
        if (!bbuf && !(bbuf = kmalloc(bs, &M_KDEV_BLK, 0)))
            return ret ? ret : -ENOMEM;

        if ((r = dev->bread(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        memcpy(bbuf, cbuf, end);

        if ((r = dev->bwrite(dd, offset/bs, 1, bbuf)) < 1)
            goto done;

        ret += end;
    }

done:
    kfree(bbuf);

    /* return the error only if nothing was written before it; if nothing
     * could be written at all, the device is full */
    if (r < 0 && !ret)
        return r;

    return ret || !size ? ret : -ENOSPC;
}

/** Reads or writes a block device's bytes from or to an iobuf (user
 * memory), a page at a time through a kernel buffer; `kdev_bread()` and
 * `kdev_bwrite()` make blocks of them. Returns how many bytes were moved,
 * or a negative errno if none were. */
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

        /* the device's end, or a fault in the iobuf */
        if ((size_t) r < len)
            break;
    }

    kfree(buf);
    return done ? (ssize_t) done : r;
}

/**
 * Reads from a device: a character device through its driver, a block
 * device in blocks.
 *
 * @param dd      the device's number
 * @param offset  where to read from, in bytes
 * @param io      where to put what is read
 * @return how many bytes were read, or a negative errno
 */
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

/**
 * Writes to a device: a character device through its driver, a block
 * device in blocks.
 *
 * @param dd      the device's number
 * @param offset  where to write, in bytes
 * @param io      what to write
 * @return how many bytes were written, or a negative errno
 */
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

/**
 * Passes an `ioctl()` to a device's driver.
 *
 * @param dd       the device's number
 * @param request  the request
 * @param argp     its argument
 * @return what the driver returns, or `-ENXIO` if it takes no `ioctl()`
 */
int kdev_ioctl(struct devid *dd, int request, void *argp)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->ioctl)
        return -ENXIO;

    return dev->ioctl(dd, request, argp);
}

/**
 * Maps a device into an address space (`mmap()` of a framebuffer).
 *
 * @param dd        the device's number
 * @param vm_space  the address space
 * @param vm_entry  the mapping
 * @return 0, or a negative errno (`-ENXIO` if the device can't be mapped)
 */
int kdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->map)
        return -ENXIO;

    return dev->map(dd, vm_space, vm_entry);
}

/**
 * Opens a device, through its driver's file operations.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @return 0, or a negative errno (`-ENXIO` if there is no such device)
 */
int kdev_file_open(struct devid *dd, struct file *file)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.open)
        return -ENXIO;

    return dev->fops.open(file);
}

/**
 * Reads an open device file, through its driver's file operations.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @param io    where to put what is read
 * @return how many bytes were read, or a negative errno
 */
ssize_t kdev_file_read(struct devid *dd, struct file *file, struct iobuf *io)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.read)
        return -ENXIO;

    return dev->fops.read(file, io);
}

/**
 * Writes to an open device file, through its driver's file operations.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @param io    what to write
 * @return how many bytes were written, or a negative errno
 */
ssize_t kdev_file_write(struct devid *dd, struct file *file, struct iobuf *io)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.write)
        return -ENXIO;

    return dev->fops.write(file, io);
}

/**
 * Moves an open device file's offset, through its driver's file
 * operations.
 *
 * @param dd      the device's number
 * @param file    the open file
 * @param offset  the offset
 * @param whence  what it is from (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`)
 * @return the new offset, or a negative errno (`-ESPIPE` for a device that
 *         can't seek)
 */
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

/**
 * Closes an open device file, through its driver's file operations.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @return 0, or a negative errno
 */
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

/**
 * Passes an `ioctl()` on an open device file to its driver's file
 * operations.
 *
 * @param dd       the device's number
 * @param file     the open file
 * @param request  the request
 * @param argp     its argument
 * @return what the driver returns, or a negative errno
 */
int kdev_file_ioctl(struct devid *dd, struct file *file, int request, void *argp)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.ioctl)
        return -ENXIO;

    return dev->fops.ioctl(file, request, argp);
}

/**
 * Asks a device's driver whether a read wouldn't have to wait.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @param size  how much would be read
 * @return whether it can be read, or a negative errno
 */
int kdev_file_can_read(struct devid *dd, struct file * file, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.can_read)
        return -ENXIO;

    return dev->fops.can_read(file, size);
}

/**
 * Asks a device's driver whether a write wouldn't have to wait.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @param size  how much would be written
 * @return whether it can be written, or a negative errno
 */
int kdev_file_can_write(struct devid *dd, struct file * file, size_t size)
{
    struct dev *dev = NULL;

    if (!(dev = kdev_get(dd)))
        return -ENXIO;

    if (!dev->fops.can_write)
        return -ENXIO;

    return dev->fops.can_write(file, size);
}

/**
 * Asks a device's driver whether a read that found no data is at the
 * end of the file.
 *
 * @param dd    the device's number
 * @param file  the open file
 * @return whether it is, or a negative errno
 */
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
 * Registers a driver as the character device of a major number. A driver
 * registered before for that major is replaced.
 *
 * @param major  the major number
 * @param dev    the driver
 */
void kdev_chrdev_register(devid_t major, struct dev *dev)
{
    chrdev[major] = dev;
    printk("kdev: registered chrdev %d: %s\n", major, dev->name);
}

/**
 * Writes what the block cache holds that was written, and what the block
 * devices hold in their write caches, to the media.
 *
 * @return 0, or a negative errno if a write failed
 */
int kdev_sync(void)
{
    int err = blkcache_flush();

    for (int i = 0; i < 256; ++i)
        if (blkdev[i] && blkdev[i]->sync && blkdev[i]->sync())
            err = -EIO;

    return err;
}

/**
 * Registers a driver as the block device of a major number. A driver
 * registered before for that major is replaced.
 *
 * @param major  the major number
 * @param dev    the driver
 */
void kdev_blkdev_register(devid_t major, struct dev *dev)
{
    blkdev[major] = dev;
    printk("kdev: registered blkdev %d: %s\n", major, dev->name);
}

/**
 * Starts the device layer, at boot. It has nothing to set up: the drivers
 * register themselves.
 */
void kdev_init()
{
    printk("kdev: initializing\n");
}
