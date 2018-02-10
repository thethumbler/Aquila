#include <core/system.h>

#include <dev/fbdev.h>
#include <dev/ramdev.h>

#include <fs/vfs.h>
#include <fs/devfs.h>

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

static ssize_t fbdev_read(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    dev_t *dev = ((struct fbdev *)node->p)->dev;
    return dev->read(node, offset, size, buf);
}

static ssize_t fbdev_write(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    dev_t *dev = ((struct fbdev *)node->p)->dev;
    return dev->write(node, offset, size, buf);
}

static int fbdev_ioctl(struct fs_node *node, int request, void *argp)
{
    struct fbdev *fb = (struct fbdev *) node->p;

    switch (request) {
    case FBIOGET_FSCREENINFO:
        memcpy(argp, fb->fix_screeninfo, sizeof(struct fb_fix_screeninfo));
        return 0;
    case FBIOGET_VSCREENINFO:
        printk("%d\n", fb->var_screeninfo->xres);
        printk("%d\n", fb->var_screeninfo->yres);
        memcpy(argp, fb->var_screeninfo, sizeof(struct fb_var_screeninfo));
        return 0;
    }

    return -1;
}

static int fbdev_probe()
{
    for (size_t i = 0; i < fbdev_cnt; ++i) {
        struct fbdev *fb = &__registered_fbs[i];

        switch (fb->type) {
            case FBDEV_TYPE_VESA:
                fbdev_vesa.probe(i, fb);
                break;
        }
    }

	return 0;
}

dev_t fbdev = {
	.name = "fbdev",
	.type = CHRDEV,
	.probe = fbdev_probe,
    .read  = fbdev_read,
	.write = fbdev_write,
    .ioctl  = fbdev_ioctl,

	.f_ops = {
		.open  = generic_file_open,
		.write = generic_file_write,
		.can_write = __can_always,
		.can_read  = __can_always,
		.eof = __eof_never,
	},
};
