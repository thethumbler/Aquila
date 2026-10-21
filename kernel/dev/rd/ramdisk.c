#include <core/system.h>
#include <core/module.h>
#include <core/string.h>

#include <boot/boot.h>
#include <dev/dev.h>
#include <mm/vm.h>

static void *rd_addr = NULL;
size_t rd_size = 0; /* XXX */

static ssize_t rd_read(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= rd_size)
        return 0;

    /* Maximum possible read size */
    size = MIN(size, rd_size - (size_t) offset);
    
    /* Copy `size' bytes from ramdev into buffer */
    memcpy(buf, (char *) rd_addr + offset, size);

    return size;
}

static ssize_t rd_write(struct devid *dd __unused, off_t offset, size_t size, void *buf)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= rd_size)
        return 0;

    /* Maximum possible write size */
    size = MIN(size, rd_size - (size_t) offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char *) rd_addr + offset, buf, size);

    return size;
}

/* the module, where the boot loader put it (after the kernel, past what
 * the early mapping covers if it is large) */
static struct vm_entry rd_vm;

static int rd_probe()
{
    extern struct boot *__kboot;
    module_t *rd_module = &__kboot->modules[0];
    rd_addr = (void *) rd_module->addr;
    rd_size = rd_module->size;

    rd_vm.base  = PAGE_ALIGN((uintptr_t) rd_addr);
    rd_vm.paddr = LMA(rd_vm.base);
    rd_vm.size  = PAGE_ROUND((uintptr_t) rd_addr + rd_size) - rd_vm.base;
    rd_vm.flags = VM_KRW;
    vm_map(&kvm_space, &rd_vm);

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
