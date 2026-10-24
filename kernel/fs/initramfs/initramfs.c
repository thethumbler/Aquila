/** \file
 * initramfs: the root filesystem from the RAM disk the boot loader
 * loaded. The RAM disk is an archive; each archiver (cpiofs, the only one)
 * is asked in turn to make a filesystem from it, and the first that can
 * becomes the root.
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>
#include <mm/mm.h>

#include <dev/ramdev.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>
#include <aquila/errno.h>

#include <ds/queue.h>
#include <boot/boot.h>

static struct vnode *rd_dev = NULL;
static struct queue *archivers = QUEUE_NEW();

/**
 * Registers a filesystem that can make a root from a RAM disk archive.
 *
 * @param fs  the filesystem, whose `load` operation reads the archive
 * @return 0, or `-ENOMEM`
 */
int initramfs_archiver_register(struct fs *fs)
{
    if (!enqueue(archivers, fs))
        return -ENOMEM;

    printk("initramfs: registered archiver: %s\n", fs->name);

    return 0;
}

/**
 * Mounts the RAM disk as the root filesystem. The kernel can't go on
 * without a root: if no archiver can read the RAM disk, it panics.
 *
 * @param module  the boot loader's module that holds the RAM disk
 * @return what mounting the root returns
 */
int load_ramdisk(module_t *module)
{
    printk("kernel: loading ramdisk\n");

    rd_dev = kmalloc(sizeof(struct vnode), &M_VNODE, M_ZERO);
    if (!rd_dev) return -ENOMEM;

    extern size_t rd_size;

    rd_dev->mode = S_IFBLK;
    rd_dev->rdev = DEV(1, 0);
    rd_dev->size = rd_size;
    rd_dev->fs   = &devfs;

    struct vnode *root = NULL;
    int err = -1;

    queue_for (node, archivers) {
        struct fs *fs = node->value;

        if (!(err = fs->load(rd_dev, &root)))
            break;
    }

    if (err) {
        printk("Error code = %d\n", err);
        panic("Could not load ramdisk\n");
    }

    return vfs_mount_root(root);
}
