/** \file
 * The RAM disk: the initial RAM disk the boot loader loaded, as block
 * device 1, 0.
 *
 * The boot loader puts the RAM disk (the first module) in memory; this
 * driver maps it, and reads and writes it with plain copies. The kernel
 * then unpacks the archive it holds as the first root filesystem (see
 * `load_ramdisk()`). Without a module, the root is on a disk, and there is
 * no RAM disk.
 */

#include <core/system.h>
#include <core/panic.h>
#include <core/arch.h>
#include <core/module.h>
#include <core/string.h>

#include <boot/boot.h>
#include <dev/dev.h>
#include <mm/vm.h>

static void *rd_addr = NULL;

/* the RAM disk's size, which the initramfs code reads too */
size_t rd_size = 0;

/** Reads from the RAM disk: up to `size` bytes at `offset`, fewer at its
 * end. Returns how many were read, 0 past the end. */
static ssize_t rd_read(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= rd_size)
        return 0;

    /* not past the end */
    size = MIN(size, rd_size - (size_t) offset);
    
    memcpy(buf, (char *) rd_addr + offset, size);

    return size;
}

/** Writes to the RAM disk: up to `size` bytes at `offset`, fewer at its
 * end. Returns how many were written, 0 past the end. */
static ssize_t rd_write(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= rd_size)
        return 0;

    /* not past the end */
    size = MIN(size, rd_size - (size_t) offset);
    
    memcpy((char *) rd_addr + offset, buf, size);

    return size;
}

/** Maps the RAM disk the boot loader loaded, and registers it as block
 * device 1. Without one (the root is on a disk), it does nothing. */
static int rd_probe()
{
    extern struct boot *__kboot;

    /* no module: the root is on a disk (root=), there's no RAM disk */
    if (!__kboot->modules_count)
        return 0;

    module_t *rd_module = &__kboot->modules[0];

    /* the first module, where the boot loader put it */
    rd_size = rd_module->size;
    rd_addr = arch_phys_map(rd_module->paddr, rd_size);

    if (!rd_addr)
        panic("ramdisk: no room to map it");

    kdev_blkdev_register(1, &rddev);

    return 0;
}

/** Returns the RAM disk's block size: 1, it is read byte by byte. */
static size_t rd_getbs(struct devid *dd __unused)
{
    return 1;
}

struct dev rddev = {
    .name  = "ramdisk",
    .probe = rd_probe,
    .bread  = rd_read,
    .bwrite = rd_write,
    .getbs = rd_getbs,
};

MODULE_INIT(rd, rd_probe, NULL)
