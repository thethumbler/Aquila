#include <core/system.h>
#include <core/string.h>
#include <console/earlycon.h>
#include <cpu/io.h>
#include <mm/vm.h>

#include "font.h"

static uintptr_t fb_paddr, fb_init = 0;
static uint32_t fb_xres, fb_yres, fb_depth,
                fb_scanline, fb_row = 0, fb_col = 0,
                fb_rows = 25, fb_cols = 80, fb_size;

static struct vmr fb_vmr = {
    .base = 0xE8000000,
};

static char *vmem   = (char *) 0xE8000000;
static char *cursor = (char *) 0xE8000000;

void earlycon_fb_register(uintptr_t paddr, uint32_t scanline, uint32_t yres, uint32_t xres, uint32_t depth)
{
    //printk("earlycon_fb_register(paddr=%p, scanline=%d, yres=%d, xres=%d, depth=%d)\n",
    //        paddr, scanline, yres, xres, depth);
    fb_paddr    = paddr;
    fb_scanline = scanline;
    fb_yres     = yres;
    fb_xres     = xres;
    fb_depth    = depth;
    fb_size     = yres * scanline * depth / 8;
}

static void earlycon_fb_scroll(int n)
{
    size_t size = n * 16 * fb_scanline;
    memcpy(vmem, vmem + size, fb_size - size);
    memset(vmem + fb_size - size, 0, size);
}

static void fb_putc(int row, int col, char c)
{
    cursor = vmem + row * 16 * fb_scanline + col * 8 * fb_depth / 8;

    char *base = cursor;

    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 8; ++j) {
            char b = 0;

            if (vga_font[(unsigned) c * 16 + i] & (1 << (7 - j)))
                b = 0xA0;

            cursor[0] = b;
            cursor[1] = b;
            cursor[2] = b;
            cursor[3] = b;

            cursor += 4;
        }

        cursor += fb_scanline - 8 * 4;
    }
}

static int earlycon_fb_putc(char c)
{
    if (!fb_init)
        return -1;

    if ((unsigned) c < 128) {
        if (c == '\n') {
            fb_col = 0;
            fb_row++;

            if (fb_row >= fb_rows) {
                earlycon_fb_scroll(1);
                fb_row = fb_rows - 1;
            }
            return 1;
        }

        fb_putc(fb_row, fb_col, c);
        ++fb_col;

        if (fb_col >= fb_cols) {
            fb_col = 0;
            fb_row++;

            if (fb_row >= fb_rows) {
                earlycon_fb_scroll(1);
                fb_row = fb_rows - 1;
            }
        }

        return 1;
    }

    return 0;
}

static int earlycon_fb_puts(char *s)
{
    while (*s)
        earlycon_fb_putc(*s++);

    return 0;
}

static void earlycon_fb_init()
{
    fb_vmr.paddr = fb_paddr;
    fb_vmr.size  = fb_size;
    fb_vmr.flags = VM_KRW | VM_NOCACHE;

    vm_map(&fb_vmr);
    fb_init = 1;

    fb_cols = fb_xres / 8;
    fb_rows = fb_yres / 16;
}

struct earlycon earlycon_fb = {
    .init = earlycon_fb_init,
    .putc = earlycon_fb_putc,
    .puts = earlycon_fb_puts,
};
