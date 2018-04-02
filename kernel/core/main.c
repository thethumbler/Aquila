#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/module.h>
#include <mm/mm.h>

#include <dev/dev.h>
#include <dev/ramdev.h>
#include <dev/console.h>

#include <fs/initramfs.h>
#include <fs/devfs.h>
#include <fs/devpts.h>
#include <fs/procfs.h>

#include <boot/multiboot.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/binfmt.h>

#include <ds/queue.h>

#include <boot/boot.h>

void kmain(struct boot *boot)
{
    printk("[0] Kernel: Initalizing Devices Subsystem\n");
    kdev_init();

    printk("[0] Kernel: Initalizing Virtual Filesystem (VFS)\n");
    vfs_init();

    printk("[0] Kernel: Loading builtin modules\n");
    modules_init();

    load_ramdisk(&boot->modules[0]);

    vfs_bind("/dev", devfs_root);

    struct uio uio = {0};
    vfs_vmknod(&vdevfs_root, "console", FS_CHRDEV, _DEV_T(5, 1), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "ptmx", FS_CHRDEV, _DEV_T(5, 2), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "kbd", FS_CHRDEV, _DEV_T(11, 0), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "fb0", FS_CHRDEV, _DEV_T(29, 0), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "hda",  FS_BLKDEV, _DEV_T(3, 0), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "hda1", FS_BLKDEV, _DEV_T(3, 1), &uio, NULL);
    vfs_vmknod(&vdevfs_root, "ttyS0", FS_CHRDEV, _DEV_T(4, 64), &uio, NULL);
    vfs_vmkdir(&vdevfs_root, "pts", &uio, NULL);

    vfs_bind("/dev/pts", devpts_root);

    printk("[0] Kernel: Loading init process\n");

    proc_t *init;
    int err;

    if ((err = binfmt_load(NULL, "/init", &init))) {
        printk("error: %d\n", err);
        panic("Can not load init process");
    }

    proc_init_spawn(init);
    for(;;);
}
