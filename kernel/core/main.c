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
    load_ramdisk(&boot->modules[0]);

    devfs_init();

    struct fs_node *dev = vfs.find(vfs_root, "dev");
    vfs.mount(dev, dev_root);

    devman_init();

    extern void devpts_init();
    devpts_init();

    //struct fs_node *pts = vfs.mkdir(dev_root, "pts");
    //extern struct fs_node *devpts_root;
    //vfs.mount(pts, devpts_root);

    /* Mount HD on /mnt .. FIXME */
    struct fs_node *mnt = vfs.find(vfs_root, "mnt");
    struct fs_node *hda1 = vfs.find(dev_root, "hda1");

    if (!hda1)
        panic("Could not load /dev/hda1");

    struct fs_node *hd  = ext2fs.load(hda1);
    vfs.mount(mnt, hd);

    printk("[0] Kernel: Loading init process\n");
    proc_t *init = load_elf("/init");

    if (!init)
        panic("Can not load init process");

    spawn_init(init);

    for(;;);
}
