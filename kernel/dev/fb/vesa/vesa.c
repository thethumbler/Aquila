/** \file
 * The VESA framebuffer: the linear framebuffer that the boot loader set
 * up, as `/dev/fb0`.
 *
 * The boot loader chose the video mode and gave its description (a VBE
 * mode info block). This driver maps the framebuffer into the kernel's
 * address space, at `ARCH_VESA_BASE`, and describes it with Linux's
 * framebuffer structures. The mode can't be changed. A process can write
 * to the framebuffer, or map it into its own address space (a shared
 * mapping only).
 */

#include <core/system.h>
#include <dev/fbdev.h>
#include <fs/devfs.h>
#include <video/vesa.h>
#include <mm/vm.h>

/* the framebuffer, in the kernel's address space */
static char *vmem = (char *) ARCH_VESA_BASE;

static struct vm_entry vesa_vm = {
    .base = ARCH_VESA_BASE,
};

/* the framebuffer's size, in bytes */
static size_t len = 0;

static struct fb_fix_screeninfo vesa_fix_screeninfo = {
    .id = "VESA FB",
};

static struct fb_var_screeninfo vesa_var_screeninfo;

/** Writes to the framebuffer at `offset`, as much as fits before its end.
 * Returns how much was written, 0 past the end. */
static ssize_t fbdev_vesa_write(struct fbdev *fb __unused, off_t offset, struct iobuf *io)
{
    if (offset < 0)
        return -EINVAL;

    if ((size_t) offset >= len)
        return 0;

    /* copy what fits into the framebuffer */
    return iobuf_in(io, (char *) vmem + offset, MIN(io->resid, len - (size_t) offset));
}

/** Maps the framebuffer, or part of it, into a process's address space.
 * Only a shared mapping is possible (`ENOTSUP` for a private one), and it
 * must lie within the framebuffer (`ENXIO` otherwise). */
static int fbdev_vesa_map(struct fbdev *fb, struct vm_space *vm_space, struct vm_entry *vm_entry)
{

    /* private mappings aren't supported */
    if (!(vm_entry->flags & VM_SHARED))
        return -ENOTSUP;

    /* the mapping must lie within the framebuffer (rounded to pages) */
    size_t fb_size = PAGE_ROUND(vesa_vm.size);

    if (vm_entry->off > fb_size || vm_entry->size > fb_size - vm_entry->off)
        return -ENXIO;

    vm_entry->paddr = vesa_vm.paddr + vm_entry->off;
    return vm_map(vm_space, vm_entry);
}

/** Sets up the framebuffer from the mode the boot loader chose: maps it
 * into the kernel's address space, and fills in its screen information
 * for `ioctl()`. */
static int fbdev_vesa_prope(int i __unused, struct fbdev *fb)
{
    struct fbdev_vesa *data = (struct fbdev_vesa *) fb->data;

    struct mode_info_block *info = data->mode_info;
    size_t size = info->y_resolution * info->lin_bytes_per_scanline;

    vesa_vm.paddr = info->phys_base_ptr;
    vesa_vm.size  = size;
    /* cached, as the early console's mapping is (arch/i386/earlycon/fb.c) */
    vesa_vm.flags = VM_KRW;

    vm_map(&kvm_space, &vesa_vm);

    len = size;


    fb->probe = fbdev_vesa_prope;
    fb->write = fbdev_vesa_write;
    fb->map   = fbdev_vesa_map;
    fb->fix_screeninfo = &vesa_fix_screeninfo;
    fb->var_screeninfo = &vesa_var_screeninfo;

    /* the screen information, which ioctl() gives */
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

    vesa_var_screeninfo.xres          = info->x_resolution;
    vesa_var_screeninfo.yres          = info->y_resolution;
    vesa_var_screeninfo.xres_virtual  = info->x_resolution;
    vesa_var_screeninfo.yres_virtual  = info->y_resolution;
    vesa_var_screeninfo.xoffset       = 0;
    vesa_var_screeninfo.yoffset       = 0;

    vesa_var_screeninfo.bits_per_pixel = info->bits_per_pixel;

    /* where each colour's bits are; if the mode doesn't say, xRGB with 8
     * bits each, or 5-6-5 for 16 bits per pixel */
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

    return 0;
}

struct fbdev fbdev_vesa = {
    .probe = fbdev_vesa_prope,
    .write = fbdev_vesa_write,
    .map   = fbdev_vesa_map,
};
