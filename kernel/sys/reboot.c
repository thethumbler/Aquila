/** \file
 * Restarting, halting and powering off (reboot), and Ctrl+Alt+Del: it
 * restarts, unless init asked for SIGINT instead (RB_DISABLE_CAD).
 */

#include <core/system.h>
#include <core/arch.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/reboot.h>
#include <dev/dev.h>

#include <aquila/errno.h>

static int cad_restarts = 1;

int kernel_reboot(unsigned cmd)
{
    /* nothing may be left in the disks' write caches */
    if (cmd == RB_AUTOBOOT || cmd == RB_HALT_SYSTEM || cmd == RB_POWER_OFF)
        kdev_sync();

    switch (cmd) {
        case RB_AUTOBOOT:
            printk("kernel: restarting system\n");
            arch_reboot();
            printk("kernel: restart failed, halting\n");
            arch_halt();
        case RB_HALT_SYSTEM:
            printk("kernel: system halted\n");
            arch_halt();
        case RB_POWER_OFF:
            printk("kernel: power down\n");
            arch_poweroff();
        case RB_ENABLE_CAD:
            cad_restarts = 1;
            return 0;
        case RB_DISABLE_CAD:
            cad_restarts = 0;
            return 0;
    }

    return -EINVAL;
}

/** from the keyboard, maybe in an interrupt */
void kernel_ctrlaltdel(void)
{
    /* right away, as a reset: writing what the caches have would wait for
     * the disk, which an interrupt can't */
    if (cad_restarts) {
        printk("kernel: restarting system\n");
        arch_reboot();
        return;
    }

    struct proc *init = proc_pid_find(1);

    if (init)
        signal_proc_send(init, SIGINT);
}
