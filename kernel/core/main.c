#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <mm/mm.h>

#include <dev/dev.h>
#include <dev/ramdev.h>
#include <dev/console.h>

#include <fs/initramfs.h>
#include <fs/devfs.h>
#include <fs/devpts.h>
#include <fs/procfs.h>
#include <fs/ext2.h>

#include <boot/multiboot.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/binfmt.h>

#include <ds/queue.h>

#include <boot/boot.h>

void kmain(struct boot *boot)
{
    printk("[0] Kernel: Initalizing Virtual Filesystem (VFS)\n");
    vfs.init();

    load_ramdisk(&boot->modules[0]);

    vfs.bind("/dev", dev_root);
    vfs.bind("/dev/pts", devpts_root);

    devman_init();

    vfs.bind("/proc", procfs_root);
    
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
