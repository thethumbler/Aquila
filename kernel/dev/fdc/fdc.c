#include <core/system.h>
#include <core/panic.h>
#include <core/module.h>

#include <cpu/io.h>
#include <dev/dev.h>
#include <bits/errno.h>

struct dev fdcdev;

int fdc_probe()
{
    //kdev_blkdev_register(2, &fdcdev);
    return 0;
}

struct dev fdcdev = {
    .name  = "fdc",
    .probe = fdc_probe,
};

MODULE_INIT(fdc, fdc_probe, NULL)
