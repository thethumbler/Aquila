#include <core/system.h>

#include <dev/fbdev.h>
#include <dev/ramdev.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#define MAX_FBDEV   2

static struct fbdev __registered_fbs[MAX_FBDEV];
static size_t fbdev_cnt = 0;

int fbdev_register(int type, void *data)
{
    if (fbdev_cnt == MAX_FBDEV)
        return -1;

    __registered_fbs[fbdev_cnt].type = type;
    __registered_fbs[fbdev_cnt++].data = data;

    return 0;
}

static ssize_t fbdev_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct fbdev *fb = &__registered_fbs[dd->minor];
    return fb->dev->read(dd, offset, size, buf);
}

static ssize_t fbdev_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct fbdev *fb = &__registered_fbs[dd->minor];
    return fb->dev->write(dd, offset, size, buf);
}

static int fbdev_ioctl(struct devid *dd, int request, void *argp)
{
    struct fbdev *fb = &__registered_fbs[dd->minor];

    switch (request) {
    case FBIOGET_FSCREENINFO:
        memcpy(argp, fb->fix_screeninfo, sizeof(struct fb_fix_screeninfo));
        return 0;
    case FBIOGET_VSCREENINFO:
        memcpy(argp, fb->var_screeninfo, sizeof(struct fb_var_screeninfo));
        return 0;
    }

    return -1;
}

int fbdev_probe()
{
    for (size_t i = 0; i < fbdev_cnt; ++i) {
        struct fbdev *fb = &__registered_fbs[i];

        switch (fb->type) {
            case FBDEV_TYPE_VESA:
                fbdev_vesa.probe(i, fb);
                break;
        }
    }

    kdev_chrdev_register(29, &fbdev);
    return 0;
}

struct dev fbdev = {
    .name = "fbdev",

    .probe  = fbdev_probe,
    .read   = fbdev_read,
    .write  = fbdev_write,
    .ioctl  = fbdev_ioctl,

    .fops = {
        .open  = posix_file_open,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,
        .lseek = posix_file_lseek,

        .can_write = __vfs_always,
        .can_read  = __vfs_never,
        .eof       = __vfs_never,
    },
};

MODULE_INIT(fbdev, fbdev_probe, NULL)
