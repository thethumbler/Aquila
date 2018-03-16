#include <core/system.h>
#include <dev/fbdev.h>
#include <fs/devfs.h>
#include <video/vesa.h>

static char *vmem = (char *) 0xCA000000;

static struct fb_fix_screeninfo vesa_fix_screeninfo = {
    .id = "VESA FB",
};

static struct fb_var_screeninfo vesa_var_screeninfo;

static ssize_t fbdev_vesa_write(struct inode *node, off_t offset, size_t size, void *buf)
{
    /* Maximum possible write size */
    size = MIN(size, node->size - offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char *) vmem + offset, buf, size);

    return size;
}

static int fbdev_vesa_prope(int i, struct fbdev *fb)
{
    struct __fbdev_vesa *data = (struct __fbdev_vesa *) fb->data;

    struct mode_info_block *info = data->mode_info;
    size_t size = info->y_resolution * info->lin_bytes_per_scanline;

    char *vmem = (char *) 0xCA000000;
    pmman.map_to(info->phys_base_ptr, (uintptr_t) vmem, size, KRW);

    fb->dev = &fbdev_vesa;
    fb->fix_screeninfo = &vesa_fix_screeninfo;
    fb->var_screeninfo = &vesa_var_screeninfo;

    char name[50] = {0};
    snprintf(name, 50, "fb%d", i);

    struct inode *fb_node = NULL;
    int ret = vfs.create(&vdev_root, name, 1, 1, 0666, &fb_node);

    if (ret)
        return ret;

    fb_node->dev = &fbdev;
    fb_node->size = size;
    fb_node->p = fb;

    /* Init fb structs */
	vesa_fix_screeninfo.smem_start = (uintptr_t) vmem;
	vesa_fix_screeninfo.smem_len   = size;
	//vesa_fix_screenifo.type
	//vesa_fix_screenifo.type_aux
	//vesa_fix_screenifo.visual
	vesa_fix_screeninfo.xpanstep = 0;
	vesa_fix_screeninfo.ypanstep = 0;
	vesa_fix_screeninfo.ywrapstep = 0;
	vesa_fix_screeninfo.line_length = info->lin_bytes_per_scanline;
	//vesa_fix_screenifo.mmio_start
	//vesa_fix_screenifo.mmio_len
	//vesa_fix_screenifo.accel
	//vesa_fix_screenifo.capabilities
	//vesa_fix_screenifo.reserved[2]
    //

    vesa_var_screeninfo.xres = info->x_resolution;
    vesa_var_screeninfo.yres = info->y_resolution;
    vesa_var_screeninfo.xres_virtual = info->x_resolution;
    vesa_var_screeninfo.yres_virtual = info->y_resolution;
    vesa_var_screeninfo.xoffset = 0;
    vesa_var_screeninfo.yoffset = 0;

    vesa_var_screeninfo.bits_per_pixel = info->bits_per_pixel;
    //vesa_var_screeninfo.grayscale
    //struct fb_bitfield red;     /* bitfield in fb mem if true color, */
    //struct fb_bitfield green;   /* else only length is significant */
    //struct fb_bitfield blue;
    //struct fb_bitfield transp;  /* transparency         */
    //vesa_var_screeninfo.nonstd
    //vesa_var_screeninfo.activate
    //vesa_var_screeninfo.height
    //vesa_var_screeninfo.width
    //vesa_var_screeninfo.accel_flags
    //vesa_var_screeninfo.pixclock
    //vesa_var_screeninfo.left_margin
    //vesa_var_screeninfo.right_margin
    //vesa_var_screeninfo.upper_margin
    //vesa_var_screeninfo.lower_margin
    //vesa_var_screeninfo.hsync_len
    //vesa_var_screeninfo.vsync_len
    //vesa_var_screeninfo.sync
    //vesa_var_screeninfo.vmode
    //vesa_var_screeninfo.rotate
    //vesa_var_screeninfo.colorspace
    //vesa_var_screeninfo.reserved[4]

    return 0;
}

dev_t fbdev_vesa = {
    .probe = fbdev_vesa_prope,
    .write = fbdev_vesa_write,
};
