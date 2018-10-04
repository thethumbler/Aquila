#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/module.h>
#include <mm/mm.h>
#include <mm/vm.h>

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

#include <console/earlycon.h>

void kmain(struct boot *boot)
{
    kdev_init();
    vfs_init();
    modules_init();

    if (boot->modules_count)
        load_ramdisk(&boot->modules[0]);
    else
        panic("No modules loaded: unable to load ramdisk");

    printk("kernel: Loading init process\n");

    proc_t *init;
    int err;

    if ((err = binfmt_load(NULL, "/init", &init))) {
        printk("error: %d\n", err);
        panic("Can not load init process");
    }


#if EARLYCON_DISABLE_ON_INIT
    earlycon_disable();
#endif

    sched_init_spawn(init);

    panic("Scheduler failed to spawn init");
}
