/**
 * \defgroup dev-tty kernel/dev/tty
 * \brief tty device
 */

#include <core/system.h>
#include <core/module.h>
#include <dev/ttydev.h>
#include <fs/posix.h>
#include <sys/sched.h>

struct dev *sttydev_mux(struct devid *dd)
{
    if (dd->minor < 64) {
        //return &vtty;
    } else {
        return &uart;
    }

    return NULL;
}

struct dev *ttydev_mux(struct devid *dd)
{
    switch (dd->minor) {
        case 0: /* /dev/tty */
            return curproc->pgrp->session->ctty;
        case 1: /* /dev/console */
            return &condev;
        case 2: /* /dev/ptmx */
            return &ptmdev;
    }

    return NULL;
}

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
