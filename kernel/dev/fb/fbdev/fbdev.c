#include <core/system.h>
#include <sys/uaccess.h>
#include <core/module.h>

#include <dev/fbdev.h>
#include <dev/ramdev.h>

#include <fs/vfs.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#define MAX_FBDEV   2

static struct fbdev registered_fbs[MAX_FBDEV];
static size_t fbdev_cnt = 0;

int fbdev_register(int type, void *data)
{
    if (fbdev_cnt == MAX_FBDEV)
        return -1;

    registered_fbs[fbdev_cnt].type = type;
    registered_fbs[fbdev_cnt++].data = data;

    return 0;
}

static struct fbdev *fbdev_get(struct devid *dd)
{
    if (dd->minor >= fbdev_cnt)
        return NULL;

    return &registered_fbs[dd->minor];
}

/* the first framebuffer, once probed, NULL if there is none */
struct fbdev *fbdev_primary(void)
{
    if (!fbdev_cnt || !registered_fbs[0].fix_screeninfo)
        return NULL;

    return &registered_fbs[0];
}

static ssize_t fbdev_read(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->read)
        return -ENXIO;

    return fb->read(fb, offset, size, buf);
}

static ssize_t fbdev_write(struct devid *dd, off_t offset, size_t size, void *buf)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->write)
        return -ENXIO;

    return fb->write(fb, offset, size, buf);
}

static int fbdev_ioctl(struct devid *dd, int request, void *argp)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb)
        return -ENXIO;

    switch (request) {
    case FBIOGET_FSCREENINFO:
        if (!fb->fix_screeninfo)
            return -ENXIO;
        if (uaccess_check(argp, sizeof(struct fb_fix_screeninfo)))
            return -EFAULT;
        memcpy(argp, fb->fix_screeninfo, sizeof(struct fb_fix_screeninfo));
        return 0;
    case FBIOGET_VSCREENINFO:
        if (!fb->var_screeninfo)
            return -ENXIO;
        if (uaccess_check(argp, sizeof(struct fb_var_screeninfo)))
            return -EFAULT;
        memcpy(argp, fb->var_screeninfo, sizeof(struct fb_var_screeninfo));
        return 0;
    }

    return -ENOTTY;
}

static int fbdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->map)
        return -ENXIO;

    return fb->map(fb, vm_space, vm_entry);
}

int fbdev_probe()
{
    for (size_t i = 0; i < fbdev_cnt; ++i) {
        struct fbdev *fb = &registered_fbs[i];

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
    .map    = fbdev_map,

    .fops = {
        .open  = posix_file_open,
        .write = posix_file_write,
        .ioctl = posix_file_ioctl,
        .lseek = posix_file_lseek,

        .can_write = __vfs_can_always,
        .can_read  = __vfs_can_never,
        .eof       = __vfs_eof_never,
    },
};

MODULE_INIT(fbdev, fbdev_probe, NULL)
