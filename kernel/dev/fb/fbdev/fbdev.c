/** \file
 * The framebuffer devices: `/dev/fb0` and up (character device 29, as on
 * Linux).
 *
 * A framebuffer driver registers its framebuffer here, and this layer
 * gives it its device, with Linux's framebuffer `ioctl()`s to describe
 * it. Writing to the device writes pixels, and `mmap()` maps the
 * framebuffer into the process. The only driver is the VESA framebuffer
 * (`dev/fb/vesa/vesa.c`).
 */

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

/**
 * Registers a framebuffer, which `fbdev_probe()` sets up with its driver.
 * The first one registered is `/dev/fb0`.
 *
 * @param type  its driver (`FBDEV_TYPE_VESA`)
 * @param data  what the driver needs to set it up (the VESA mode's
 *              description)
 * @return 0, or `-ENOSPC` if there are `MAX_FBDEV` already
 */
int fbdev_register(int type, void *data)
{
    if (fbdev_cnt == MAX_FBDEV)
        return -ENOSPC;

    registered_fbs[fbdev_cnt].type = type;
    registered_fbs[fbdev_cnt++].data = data;

    return 0;
}

/** Returns the framebuffer of a device number, or NULL if there's none. */
static struct fbdev *fbdev_get(struct devid *dd)
{
    if (dd->minor >= fbdev_cnt)
        return NULL;

    return &registered_fbs[dd->minor];
}

/**
 * Returns the first framebuffer, once it is set up, for the virtual
 * terminals.
 *
 * @return the framebuffer, or NULL if there is none
 */
struct fbdev *fbdev_primary(void)
{
    if (!fbdev_cnt || !registered_fbs[0].fix_screeninfo)
        return NULL;

    return &registered_fbs[0];
}

/** Reads from a framebuffer, through its driver. */
static ssize_t fbdev_read(struct devid *dd, off_t offset, struct iobuf *io)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->read)
        return -ENXIO;

    return fb->read(fb, offset, io);
}

/** Writes to a framebuffer, through its driver. */
static ssize_t fbdev_write(struct devid *dd, off_t offset, struct iobuf *io)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->write)
        return -ENXIO;

    return fb->write(fb, offset, io);
}

/** Handles Linux's framebuffer `ioctl()`s: the screen information, which
 * can be read but not changed, and panning, which isn't supported. */
static int fbdev_ioctl(struct devid *dd, int request, void *argp)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb)
        return -ENXIO;

    switch (request) {
    case FBIOGET_FSCREENINFO:
        if (!fb->fix_screeninfo)
            return -ENXIO;
        return copyout(argp, fb->fix_screeninfo, sizeof(struct fb_fix_screeninfo));
    case FBIOGET_VSCREENINFO:
        if (!fb->var_screeninfo)
            return -ENXIO;
        return copyout(argp, fb->var_screeninfo, sizeof(struct fb_var_screeninfo));
    case FBIOPUT_VSCREENINFO: {
        /* the mode can't be changed: only a request for the mode it is
         * already succeeds, and the caller gets the mode back */
        struct fb_var_screeninfo *var = fb->var_screeninfo, _new, *new = &_new;

        if (!var)
            return -ENXIO;
        if (copyin(new, argp, sizeof(*new)))
            return -EFAULT;

        if (new->xres != var->xres || new->yres != var->yres ||
            new->xres_virtual > var->xres_virtual ||
            new->yres_virtual > var->yres_virtual ||
            (new->bits_per_pixel && new->bits_per_pixel != var->bits_per_pixel) ||
            new->xoffset || new->yoffset)
            return -EINVAL;

        return copyout(argp, var, sizeof(struct fb_var_screeninfo));
    }
    case FBIOPAN_DISPLAY: {
        /* no panning: the virtual screen is the visible one */
        struct fb_var_screeninfo new;

        if (copyin(&new, argp, sizeof(new)))
            return -EFAULT;

        return new.xoffset || new.yoffset ? -EINVAL : 0;
    }
    }

    return -ENOTTY;
}

/** Maps a framebuffer into a process's address space, through its
 * driver. */
static int fbdev_map(struct devid *dd, struct vm_space *vm_space, struct vm_entry *vm_entry)
{
    struct fbdev *fb = fbdev_get(dd);

    if (!fb || !fb->map)
        return -ENXIO;

    return fb->map(fb, vm_space, vm_entry);
}

/**
 * Sets up the registered framebuffers with their drivers, and registers
 * the framebuffer devices.
 *
 * @return 0
 */
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
