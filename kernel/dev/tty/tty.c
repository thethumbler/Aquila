#include <core/system.h>
#include <core/module.h>
#include <dev/ttydev.h>
#include <fs/posix.h>

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
            //return cur_thread->owner->pgrp->session->ttydev;
            break;
        case 1: /* /dev/console */
            return &condev;
            break;
        case 2: /* /dev/ptmx */
            return &ptmdev;
            break;
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
