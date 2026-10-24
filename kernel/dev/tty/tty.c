/** \file
 * The terminal devices' numbers: which driver each one is.
 *
 * Major 4 is the terminals themselves: `/dev/tty0` to `/dev/tty6`, the
 * virtual terminals (minor 0 is the one in the foreground), and
 * `/dev/ttyS0` and up, the UARTs (minors 64 and up). Major 5 is the
 * special ones: `/dev/tty` (the process's controlling terminal),
 * `/dev/console` and `/dev/ptmx`. These are Linux's numbers.
 */

#include <core/system.h>
#include <core/module.h>
#include <dev/ttydev.h>
#include <fs/posix.h>
#include <sys/sched.h>
#include <dev/tty.h>
#include <dev/vt.h>
#include <fs/vfs.h>

/**
 * Picks the driver of a terminal of major 4, by its minor: the virtual
 * terminals' for minors up to `VT_COUNT`, the UARTs' for 64 and up.
 *
 * @param dd  the device's number
 * @return its driver, or NULL if the kernel has none for it
 */
struct dev *sttydev_mux(struct devid *dd)
{
#ifdef CONFIG_DEV_UART
    if (dd->minor >= 64)
        return &uart;
#endif

#ifdef CONFIG_DEV_CONSOLE
    if (dd->minor <= VT_COUNT)
        return &vtdev;
#endif

    return NULL;
}

/** Opens `/dev/tty`: the file is made the controlling terminal's device
 * instead. Fails with `ENXIO` if the process has no controlling
 * terminal. */
static int ctty_open(struct file *file)
{
    struct tty *tty = curproc->pgrp->session->ctty;
    struct vnode *vnode = file->vnode;

    if (!tty || !tty->vnode)
        return -ENXIO;

    tty->vnode->ref++;
    file->vnode = tty->vnode;

    int err = kdev_file_open(&VNODE_DEV(tty->vnode), file);

    if (err) {
        vfs_close(tty->vnode);
        file->vnode = vnode;
        return err;
    }

    vfs_close(vnode);
    return 0;
}

static struct dev cttydev = {
    .name = "ctty",
    .fops = {
        .open = ctty_open,
    },
};

/**
 * Picks the driver of a terminal device of major 5, by its minor.
 *
 * @param dd  the device's number
 * @return its driver, or NULL if the kernel has none for it
 */
struct dev *ttydev_mux(struct devid *dd)
{
    switch (dd->minor) {
        case 0: /* /dev/tty */
            return &cttydev;
        case 1: /* /dev/console: the first virtual terminal or, without
                 * them, the first UART (uart_get()) */
#if defined(CONFIG_DEV_CONSOLE)
            return &vtdev;
#elif defined(CONFIG_DEV_UART)
            return &uart;
#else
            return NULL;
#endif
#ifdef CONFIG_DEV_PTY
        case 2: /* /dev/ptmx */
            return &ptmdev;
#endif
    }

    return NULL;
}

/**
 * Registers the terminal devices, majors 4 and 5.
 *
 * @return 0
 */
int ttydev_probe()
{
    kdev_chrdev_register(4, &sttydev);
    kdev_chrdev_register(5, &ttydev);
    return 0;
}

struct dev sttydev = {
    .name  = "sttydev",
    .mux   = sttydev_mux,
};

struct dev ttydev = {
    .name  = "ttydev",
    .mux   = ttydev_mux,
};

MODULE_INIT(ttydev, ttydev_probe, NULL)
