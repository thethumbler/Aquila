/** \file
 * The kernel's generic entry point, `kmain()`.
 *
 * The architecture's boot code sets up the CPU and memory, then calls
 * `kmain()`, which sets up the devices and filesystems, mounts the root
 * filesystem, and starts the first process, `/init`. See
 * docs/kernel/booting.md.
 */

#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/module.h>
#include <core/kargs.h>
#include <core/arch.h>
#include <mm/mm.h>
#include <mm/pmap.h>
#include <mm/vm.h>
#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/initramfs.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/binfmt.h>
#include <boot/boot.h>
#include <console/earlycon.h>

#include <ds/hashmap.h>

/**
 * Starts the kernel's generic part, once the architecture has set up the
 * CPU, memory and the platform. It mounts the root filesystem and runs
 * `/init`, and doesn't return.
 *
 * @param boot  what the boot loader passed: the memory map, the modules,
 *              the command line
 */
void kmain(struct boot *boot)
{
    /* check the kernel's allocator, if asked to (tests/kvmem) */
    if (!kargs_get("kvmem.test", NULL))
        kvmem_selftest();

    if (!kargs_get("kvmem.guard", NULL))
        kvmem_guardtest();

    kdev_init();
    vfs_init();
    modules_init();

    /* mount the root filesystem: a disk partition (root=/dev/hda2), or
     * the RAM disk that the boot loader loaded */
    const char *root = NULL, *rootfstype = NULL;
    int err;

    if (!kargs_get("root", &root) && root) {
        kargs_get("rootfstype", &rootfstype);

        if ((err = vfs_mount_root_dev(root, rootfstype))) {
            printk("kernel: can't mount %s: error %d\n", root, -err);
            panic("could not mount the root filesystem");
        }
    } else if (boot->modules_count) {
        load_ramdisk(&boot->modules[0]);
    } else {
        panic("No modules loaded: unable to load ramdisk");
    }

    /* mount devfs on /dev, with /dev/console, so that init can write to
     * the console right away */
    struct uio uio = {.mask = 022};

    if (!vfs_mount("devfs", "/dev", 0, NULL, &uio))
        vfs_mknod("/dev/console", S_IFCHR | 0600, DEV(5, 1), &uio, NULL);

    printk("kernel: loading init process\n");

    struct proc *init;

    if ((err = proc_new(&init))) {
        panic("failed to allocate process structure for init");
    }

    initproc = init;

    curthread = (struct thread *) init->threads.head->value;
    curproc   = init;

    pmap_switch(init->vm_space.pmap);

    const char *init_p = "/init";

    if ((err = binfmt_load(init, init_p, &init))) {
        printk("kernel: failed to load %s: error: %d\n", init_p, -err);
        panic("could not load init process");
    }

    arch_proc_init(init);

    /* init's argument is the module's command line (none when booting
     * from a disk) */
    char *cmdline = boot->modules_count ? boot->modules[0].cmdline : "";
    char *argp[] = {cmdline, 0};
    char *envp[] = {0};

    init_execve(init, 1, argp, 0, envp);

#if EARLYCON_DISABLE_ON_INIT
    earlycon_disable();
#endif

    sched_init_spawn(init);

    panic("Scheduler failed to spawn init");
}
