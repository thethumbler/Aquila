#include <core/system.h>
#include <core/string.h>

#include <boot/boot.h>
#include <dev/dev.h>

static void   *rd_addr = NULL;
size_t rd_size = 0; /* XXX */

static ssize_t rd_read(struct devid *dd __unused, off_t offset, size_t _size, void *buf)
{
    /* Maximum possible read size */
    ssize_t size = MIN(_size, rd_size - offset);
    
    /* Copy `size' bytes from ramdev into buffer */
    memcpy(buf, (char *) rd_addr + offset, size);

    return size;
}

static ssize_t rd_write(struct devid *dd __unused, off_t offset, size_t _size, void *buf)
{
    /* Maximum possible write size */
    ssize_t size = MIN(_size, rd_size - offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char *) rd_addr + offset, buf, size);

    return size;
}

static int rd_probe()
{
    extern struct boot *__kboot;
    module_t *rd_module = &__kboot->modules[0];
    rd_addr = (void *) rd_module->addr;
    rd_size = rd_module->size;

    kdev_blkdev_register(1, &rddev);

    return 0;
}

static size_t rd_getbs(struct devid *dd __unused)
{
    return 1;   /* FIXME */
}

struct dev rddev = {
    .name  = "ramdisk",
    .probe = rd_probe,
    .read  = rd_read,
    .write = rd_write,
    .getbs = rd_getbs,
};

MODULE_INIT(rd, rd_probe, NULL)
