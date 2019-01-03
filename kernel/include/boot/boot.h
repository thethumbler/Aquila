#ifndef _BOOT_H
#define _BOOT_H

#include <core/system.h>

typedef struct {
    void *addr;
    size_t size;
    char *cmdline;
} module_t;

enum mmap_type {
    MMAP_USABLE   = 1,
    MMAP_RESERVED = 2
};

typedef struct {
    enum mmap_type type;
    uintptr_t start;
    uintptr_t end;
} mmap_t;

struct boot {
    char *cmdline;
    uintptr_t total_mem;
    
    int modules_count;
    int mmap_count;
    module_t *modules;
    mmap_t *mmap;
};

#include <boot/multiboot.h>

#ifdef MULTIBOOT_GFX
#include <dev/fbdev.h>
#include <video/vbe.h>
#include <video/vesa.h>
#endif

static inline int get_multiboot_mmap_count(multiboot_info_t *info)
{
    int count = 0;
    uint32_t _mmap = info->mmap_addr;
    multiboot_mmap_t *mmap = (multiboot_mmap_t *)(uintptr_t)_mmap;
    uint32_t mmap_end = _mmap + info->mmap_length;

    while (_mmap < mmap_end) {
        //if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE)
        ++count;
        _mmap += mmap->size + sizeof(uint32_t);
        mmap   = (multiboot_mmap_t *)(uintptr_t) _mmap;
    }
    return count;
}

static inline void 
build_multiboot_mmap(multiboot_info_t *info, mmap_t *boot_mmap)
{
    uint32_t _mmap = info->mmap_addr;
    multiboot_mmap_t *mmap = (multiboot_mmap_t *)(uintptr_t) _mmap;
    uint32_t mmap_end = _mmap + info->mmap_length;

    while (_mmap < mmap_end) {

        *boot_mmap = (mmap_t) {
            .type  = mmap->type,
            .start = mmap->addr,
            .end = mmap->addr + mmap->len
        };

        ++boot_mmap;
        _mmap += mmap->size + sizeof(uint32_t);
        mmap   = (multiboot_mmap_t *)(uintptr_t) _mmap;
    }
}

static inline void 
build_multiboot_modules(multiboot_info_t *info, module_t *modules)
{
    extern char *lower_kernel_heap;
    multiboot_module_t *mods = (multiboot_module_t *)(uintptr_t) info->mods_addr;

    for (unsigned i = 0; i < info->mods_count; ++i) {
        modules[i] = (module_t) {
            .addr = (void *) VMA((uintptr_t) mods[i].mod_start),
            .size = mods[i].mod_end - mods[i].mod_start,
            .cmdline = (char *) VMA((uintptr_t) mods[i].cmdline)
        };

        if ((uintptr_t) lower_kernel_heap < mods[i].mod_start)
            lower_kernel_heap = (char *)(uintptr_t) mods[i].mod_end;
    }
}

static inline struct boot *process_multiboot_info(multiboot_info_t *info)
{
    static struct boot boot;

    boot.cmdline = (char *) VMA((uintptr_t) info->cmdline);
    boot.total_mem = info->mem_lower + info->mem_upper;

    boot.mmap_count = get_multiboot_mmap_count(info);
    static mmap_t mmap[32];
    boot.mmap = mmap;
    build_multiboot_mmap(info, boot.mmap);

#ifdef MULTIBOOT_GFX
    /* We report video memory as mmap region */
    boot.mmap[boot.mmap_count].type = MMAP_RESERVED;

    struct vbe_info_block  *vinfo = (struct vbe_info_block *)(uintptr_t)  info->vbe_control_info;
    struct mode_info_block *minfo = (struct mode_info_block *)(uintptr_t) info->vbe_mode_info;

    boot.mmap[boot.mmap_count].start = minfo->phys_base_ptr;
    boot.mmap[boot.mmap_count].end   = minfo->phys_base_ptr + minfo->y_resolution * minfo->lin_bytes_per_scanline;

    boot.mmap_count++;

    extern void earlycon_fb_register(uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t);

    uintptr_t vaddr    = minfo->phys_base_ptr;
    uint32_t  scanline = minfo->lin_bytes_per_scanline;
    uint32_t  yres     = minfo->y_resolution;
    uint32_t  xres     = minfo->x_resolution;
    uint32_t  depth    = minfo->bits_per_pixel;

    earlycon_fb_register(vaddr, scanline, yres, xres, depth);

    static struct fbdev_vesa data;
    data.vbe_info  = (struct vbe_info_block *) VMA(vinfo);
    data.mode_info = (struct mode_info_block *) VMA(minfo);

    /* And register fbdev of type `vesa' */
    fbdev_register(FBDEV_TYPE_VESA, &data);
#endif

    boot.modules_count = info->mods_count;
    static module_t modules[32];
    boot.modules = modules;
    build_multiboot_modules(info, boot.modules);

    return &boot;
}

#endif
