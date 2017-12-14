#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <mm/mm.h>

#include <dev/dev.h>
#include <dev/ramdev.h>
#include <dev/console.h>

#include <fs/initramfs.h>
#include <fs/devfs.h>
#include <fs/ext2.h>

#include <boot/multiboot.h>

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/elf.h>

#include <ds/queue.h>

#include <boot/boot.h>

void kmain(struct boot *boot)
{
    printk("[0] Kernel: Initalizing Virtual Filesystem (VFS)\n");
    vfs.init();

    load_ramdisk(&boot->modules[0]);

    devfs_init();

    vfs.mount("/dev", dev_root);

    devman_init();

    extern void devpts_init();
    devpts_init();

    printk("[0] Kernel: Loading init process\n");
    proc_t *init = load_elf("/init");

    if (!init)
        panic("Can not load init process");

    spawn_init(init);

    for(;;);
}
