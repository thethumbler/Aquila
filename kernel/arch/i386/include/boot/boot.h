#ifndef _X86_BOOT_H
#define _X86_BOOT_H

#include_next <boot/boot.h>
#include <boot/multiboot.h>

#ifdef MULTIBOOT_GFX
#include <dev/fbdev.h>
#include <video/vbe.h>
#include <video/vesa.h>
#endif

/*
 * x86: what a Multiboot boot loader passed, turned into the kernel's
 * struct boot (boot/boot.h): the memory map, the modules (the RAM disk),
 * the command line, the kernel's symbols, and the framebuffer.
 */

#define MULTIBOOT_MMAP_MAX      32
#define MULTIBOOT_MODULES_MAX   32

/*
 * Converts the Multiboot memory map into the kernel's mmap_t array,
 * storing at most `max` entries, and returns how many it stored. On i386,
 * regions starting above 4 GiB are skipped, and regions crossing 4 GiB are
 * cut there, since the kernel can't address them anyway.
 */
static inline int
build_multiboot_mmap(multiboot_info_t *info, mmap_t *boot_mmap, int max)
{
    int count = 0;
    uint32_t _mmap = info->mmap_addr;
    multiboot_mmap_t *mmap = (multiboot_mmap_t *)(uintptr_t) _mmap;
    uint32_t mmap_end = _mmap + info->mmap_length;

    while (_mmap < mmap_end && count < max) {
        uint64_t start = mmap->addr;
        uint64_t end   = mmap->addr + mmap->len;

#if ARCH_BITS==32
        if (start < 0x100000000ULL) {
            if (end > 0xFFFFFFFFULL || end < start)
                end = 0xFFFFFFFFULL;
#else
        {
#endif

            boot_mmap[count++] = (mmap_t) {
                .type  = mmap->type,
                .start = (uintptr_t) start,
                .end   = (uintptr_t) end,
            };
        }

        _mmap += mmap->size + sizeof(uint32_t);
        mmap   = (multiboot_mmap_t *)(uintptr_t) _mmap;
    }

    return count;
}

/* Converts the Multiboot modules into the kernel's module_t array, storing
 * at most `max` of them, and returns how many it stored. */
static inline int
build_multiboot_modules(multiboot_info_t *info, module_t *modules, int max)
{
    multiboot_module_t *mods = (multiboot_module_t *)(uintptr_t) info->mods_addr;
    int count = 0;

    for (unsigned i = 0; i < info->mods_count && count < max; ++i) {
        modules[count++] = (module_t) {
            .paddr = mods[i].mod_start,
            .size = mods[i].mod_end - mods[i].mod_start,
            .cmdline = mods[i].cmdline ?
                (char *) VMA((uintptr_t) mods[i].cmdline) : "",
        };
    }

    return count;
}

/* Reads the Multiboot information that the boot loader passed into the
 * kernel's struct boot, and returns it. */
static inline struct boot *process_multiboot_info(multiboot_info_t *info)
{
    static struct boot boot;

    if ((info->flags & MULTIBOOT_INFO_CMDLINE) && info->cmdline)
        boot.cmdline = (char *) VMA((uintptr_t) info->cmdline);
    else
        boot.cmdline = "";

    if (info->flags & MULTIBOOT_INFO_MEMORY)
        boot.total_mem = info->mem_lower + info->mem_upper;

    /* the kernel's section headers, if they're of its own ELF class */
    if ((info->flags & MULTIBOOT_INFO_ELF_SHDR) && info->u.elf_sec.size == sizeof(elf_shdr_t)) {
        boot.shdr = (elf_shdr_t *) VMA((uintptr_t) info->u.elf_sec.addr);
        boot.shdr_num = info->u.elf_sec.num;

        for (size_t i = 0; i < boot.shdr_num; ++i) {
            elf_shdr_t *shdr = &boot.shdr[i];

            if (shdr->sh_type == SHT_SYMTAB && !boot.symtab) {
                shdr->sh_addr = VMA((uintptr_t) shdr->sh_addr);
                boot.symtab = shdr;
                boot.symnum = shdr->sh_size / sizeof(elf_sym_t);

                /* the symbols' names are in the string table that the
                 * symbol table links to, not necessarily the first one */
                if (shdr->sh_link && shdr->sh_link < boot.shdr_num) {
                    elf_shdr_t *strtab = &boot.shdr[shdr->sh_link];
                    strtab->sh_addr = VMA((uintptr_t) strtab->sh_addr);
                    boot.strtab = strtab;
                }
            }
        }

        /* the symbols are useless without their string table */
        if (!boot.strtab) {
            boot.symtab = NULL;
            boot.symnum = 0;
        }
    }

    /* one more entry, for the framebuffer's region */
    static mmap_t mmap[MULTIBOOT_MMAP_MAX + 1];
    boot.mmap = mmap;
    boot.mmap_count = 0;

    if (info->flags & MULTIBOOT_INFO_MEM_MAP)
        boot.mmap_count = build_multiboot_mmap(info, boot.mmap, MULTIBOOT_MMAP_MAX);

#ifdef MULTIBOOT_GFX
    /*
     * The linear framebuffer, from the VBE mode information (GRUB) or from
     * Multiboot's framebuffer fields (Limine), if a mode was set. minfo is
     * read here, through the identity mapping; the fbdev uses the kernel's
     * mapping of the structures later.
     */
    struct mode_info_block *minfo = NULL;
    static struct fbdev_vesa data;

    if ((info->flags & MULTIBOOT_INFO_VIDEO_INFO) && info->vbe_mode_info) {
        minfo = (struct mode_info_block *)(uintptr_t) info->vbe_mode_info;
        data.vbe_info  = (struct vbe_info_block *) VMA(info->vbe_control_info);
        data.mode_info = (struct mode_info_block *) VMA(info->vbe_mode_info);
    } else if ((info->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) &&
               info->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
               !(info->framebuffer_addr >> 32)) {
        /* describe it as a VBE mode, for the vesa fbdev */
        static struct mode_info_block fb_minfo;

        fb_minfo.phys_base_ptr          = info->framebuffer_addr;
        fb_minfo.bytes_per_scanline     = info->framebuffer_pitch;
        fb_minfo.lin_bytes_per_scanline = info->framebuffer_pitch;
        fb_minfo.x_resolution           = info->framebuffer_width;
        fb_minfo.y_resolution           = info->framebuffer_height;
        fb_minfo.bits_per_pixel         = info->framebuffer_bpp;

        /* the positions and sizes of red, green and blue */
        uint8_t *color = info->framebuffer_color_info;
        fb_minfo.red_field_position     = color[0];
        fb_minfo.red_mask_size          = color[1];
        fb_minfo.green_field_position   = color[2];
        fb_minfo.green_mask_size        = color[3];
        fb_minfo.blue_field_position    = color[4];
        fb_minfo.blue_mask_size         = color[5];

        minfo = &fb_minfo;
        data.vbe_info  = NULL;
        data.mode_info = &fb_minfo;
    }

    if (minfo) {
        uintptr_t paddr    = minfo->phys_base_ptr;
        uint32_t  scanline = minfo->lin_bytes_per_scanline;
        uint32_t  yres     = minfo->y_resolution;
        uint32_t  xres     = minfo->x_resolution;
        uint32_t  depth    = minfo->bits_per_pixel;

        /* the framebuffer's memory is a reserved region of the map */
        boot.mmap[boot.mmap_count] = (mmap_t) {
            .type  = MMAP_RESERVED,
            .start = paddr,
            .end   = paddr + yres * scanline,
        };

        boot.mmap_count++;

        extern void earlycon_fb_register(uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t);
        earlycon_fb_register(paddr, scanline, yres, xres, depth);

        /* and it is a vesa fbdev, /dev/fb0 */
        fbdev_register(FBDEV_TYPE_VESA, &data);
    }
#endif

    static module_t modules[MULTIBOOT_MODULES_MAX];
    boot.modules = modules;
    boot.modules_count = 0;

    if (info->flags & MULTIBOOT_INFO_MODS)
        boot.modules_count = build_multiboot_modules(info, boot.modules, MULTIBOOT_MODULES_MAX);

    return &boot;
}

#endif /* _X86_BOOT_H */
