#include <core/system.h>
#include <dev/fbdev.h>
#include <fs/devfs.h>
#include <video/vesa.h>
#include <mm/vm.h>

static char *vmem = (char *) 0xE0000000;

static struct vm_entry vesa_vm = {
    .base = 0xE0000000,
};

static size_t len = 0;

static struct fb_fix_screeninfo vesa_fix_screeninfo = {
    .id = "VESA FB",
};

static struct fb_var_screeninfo vesa_var_screeninfo;

static ssize_t fbdev_vesa_write(struct fbdev *fb, off_t offset, size_t size, void *buf)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= len)
        return 0;

    /* Maximum possible write size */
    size = MIN(size, len - (size_t) offset);
    
    /* Copy `size' bytes from buffer to ramdev */
    memcpy((char *) vmem + offset, buf, size);

    return size;
}

static int fbdev_vesa_map(struct fbdev *fb, struct vm_space *vm_space, struct vm_entry *vm_entry)
{

    /* We do not support private maps */
    if (!(vm_entry->flags & VM_SHARED))
        return -ENOTSUP;

    /* Mapping must lie within the (page rounded) framebuffer */
    size_t fb_size = PAGE_ROUND(vesa_vm.size);

    if (vm_entry->off > fb_size || vm_entry->size > fb_size - vm_entry->off)
        return -ENXIO;

    vm_entry->paddr = vesa_vm.paddr + vm_entry->off;
    return vm_map(vm_space, vm_entry);
}

static int fbdev_vesa_prope(int i __unused, struct fbdev *fb)
{
    struct fbdev_vesa *data = (struct fbdev_vesa *) fb->data;

    struct mode_info_block *info = data->mode_info;
    size_t size = info->y_resolution * info->lin_bytes_per_scanline;

    vesa_vm.paddr = info->phys_base_ptr;
    vesa_vm.size  = size;
    vesa_vm.flags = VM_KRW | VM_NOCACHE;

    vm_map(&kvm_space, &vesa_vm);

    //memset(vmem, 0x5A, size);

    len = size;


    fb->probe = fbdev_vesa_prope;
    fb->write = fbdev_vesa_write;
    fb->map   = fbdev_vesa_map;
    fb->fix_screeninfo = &vesa_fix_screeninfo;
    fb->var_screeninfo = &vesa_var_screeninfo;

    /* Init fb structs */
    vesa_fix_screeninfo.smem_start = (uintptr_t) vmem;
    vesa_fix_screeninfo.smem_len   = size;
    vesa_fix_screeninfo.type          = FB_TYPE_PACKED_PIXELS;
    vesa_fix_screeninfo.visual        = FB_VISUAL_TRUECOLOR;
    vesa_fix_screeninfo.xpanstep      = 0;
    vesa_fix_screeninfo.ypanstep      = 0;
    vesa_fix_screeninfo.ywrapstep     = 0;
    vesa_fix_screeninfo.line_length   = info->lin_bytes_per_scanline;
    vesa_fix_screeninfo.mmio_start    = info->phys_base_ptr;
    vesa_fix_screeninfo.mmio_len      = size;
    //vesa_fix_screenifo.accel
    //vesa_fix_screenifo.capabilities
    //vesa_fix_screenifo.reserved[2]
    //

    vesa_var_screeninfo.xres          = info->x_resolution;
    vesa_var_screeninfo.yres          = info->y_resolution;
    vesa_var_screeninfo.xres_virtual  = info->x_resolution;
    vesa_var_screeninfo.yres_virtual  = info->y_resolution;
    vesa_var_screeninfo.xoffset       = 0;
    vesa_var_screeninfo.yoffset       = 0;

    vesa_var_screeninfo.bits_per_pixel = info->bits_per_pixel;

    /* the colors' bits; xRGB if the mode doesn't tell (8 bits each, or
     * 5-6-5 for 16 bits per pixel) */
    if (info->red_mask_size && info->green_mask_size && info->blue_mask_size) {
        vesa_var_screeninfo.red   = (struct fb_bitfield) {info->red_field_position, info->red_mask_size, 0};
        vesa_var_screeninfo.green = (struct fb_bitfield) {info->green_field_position, info->green_mask_size, 0};
        vesa_var_screeninfo.blue  = (struct fb_bitfield) {info->blue_field_position, info->blue_mask_size, 0};
    } else if (info->bits_per_pixel == 16) {
        vesa_var_screeninfo.red   = (struct fb_bitfield) {11, 5, 0};
        vesa_var_screeninfo.green = (struct fb_bitfield) {5, 6, 0};
        vesa_var_screeninfo.blue  = (struct fb_bitfield) {0, 5, 0};
    } else {
        vesa_var_screeninfo.red   = (struct fb_bitfield) {16, 8, 0};
        vesa_var_screeninfo.green = (struct fb_bitfield) {8, 8, 0};
        vesa_var_screeninfo.blue  = (struct fb_bitfield) {0, 8, 0};
    }

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

struct fbdev fbdev_vesa = {
    .probe = fbdev_vesa_prope,
    .write = fbdev_vesa_write,
    .map   = fbdev_vesa_map,
};
