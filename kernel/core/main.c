/**
 * \defgroup core kernel/core
 * \brief core system components
 */

#include <core/printk.h>
#include <core/panic.h>
#include <core/string.h>
#include <core/module.h>
#include <core/arch.h>
#include <mm/mm.h>
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
 * \ingroup core
 * \brief entry point of the kernel
 *
 * This is the function that gets executed after the bootstrapping and
 * platform initialization is completed.
 *
 * \param boot structure representing system state after bootstrapping
 */
void kmain(struct boot *boot)
{
    kdev_init();
    vfs_init();
    modules_init();

    if (boot->modules_count)
        load_ramdisk(&boot->modules[0]);
    else
        panic("No modules loaded: unable to load ramdisk");

    printk("kernel: loading init process\n");

    struct proc *init;
    int err;

    if ((err = proc_new(&init))) {
        panic("failed to allocate process structure for init");
    }

    cur_thread = (struct thread *) init->threads.head;
    cur_thread->owner = init;
    arch_pmap_switch(init->vm_space.pmap);

    const char *init_p = "/init";

    if ((err = binfmt_load(init, init_p, &init))) {
        printk("failed to load %s: error: %d\n", init_p, -err);
        panic("could not load init process");
    }

    //cur_thread = NULL;

    arch_proc_init(init);

    char *cmdline = boot->modules[0].cmdline;
    char *argp[] = {cmdline, 0};
    char *envp[] = {0};

    arch_init_execve(init, 2, argp, 1, envp);

#if EARLYCON_DISABLE_ON_INIT
    earlycon_disable();
#endif

    sched_init_spawn(init);

    panic("Scheduler failed to spawn init");
}
