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

#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/binfmt.h>

#include <ds/queue.h>

#include <boot/boot.h>

void kmain(struct boot *boot)
{
    kdev_init();
    vfs_init();
    modules_init();

    load_ramdisk(&boot->modules[0]);

    printk("Kernel: Loading init process\n");

    proc_t *init;
    int err;

    if ((err = binfmt_load(NULL, "/init", &init))) {
        printk("error: %d\n", err);
        panic("Can not load init process");
    }

    sched_init_spawn(init);
    for(;;);
}
