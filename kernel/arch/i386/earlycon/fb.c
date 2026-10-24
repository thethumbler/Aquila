/** \file
 * x86: the early console on the boot loader's framebuffer.
 *
 * The boot loader may set up a linear framebuffer (Multiboot's VBE or
 * framebuffer information), which the boot code registers here. Text is
 * drawn with the 8x16 font, in 32-bit pixels only, and the screen scrolls
 * by copying it up a line.
 */

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

static struct vm_entry fb_vmr = {
    .base = ARCH_EARLYFB_BASE,
};

static char *vmem   = (char *) ARCH_EARLYFB_BASE;
static char *cursor = (char *) ARCH_EARLYFB_BASE;

/**
 * Registers the boot loader's framebuffer, for the early console. The
 * boot code calls it, before it is mapped.
 *
 * @param paddr     its physical address
 * @param scanline  the size of a line, in bytes
 * @param yres      its height, in pixels
 * @param xres      its width, in pixels
 * @param depth     the bits per pixel
 */
void earlycon_fb_register(uintptr_t paddr, uint32_t scanline, uint32_t yres, uint32_t xres, uint32_t depth)
{
    fb_paddr    = paddr;
    fb_scanline = scanline;
    fb_yres     = yres;
    fb_xres     = xres;
    fb_depth    = depth;
    fb_size     = yres * scanline; /* scanline is already in bytes */
}

/**
 * Says whether the boot loader's framebuffer can be the early console.
 *
 * @return 1 if one was registered and has 32 bits per pixel, 0 if not
 */
int earlycon_fb_available(void)
{
    /* fb_putc() only draws 32-bit pixels */
    return fb_paddr && fb_size && fb_depth == 32;
}

/** Scrolls the screen up by `n` lines of text, clearing the bottom. */
static void earlycon_fb_scroll(int n)
{
    size_t size = n * 16 * fb_scanline;
    memmove(vmem, vmem + size, fb_size - size);  /* the ranges overlap */
    memset(vmem + fb_size - size, 0, size);
}

/** Draws a character at a row and a column of text. */
static void fb_putc(int row, int col, char c)
{
    cursor = vmem + row * 16 * fb_scanline + col * 8 * fb_depth / 8;

    char *base = cursor;

    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 8; ++j) {
            char b = 0;

            if (font_8x16[(unsigned) c * 16 + i] & (1 << (7 - j)))
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

/** Writes a character at the cursor, scrolling when it goes past the last
 * line. Returns 1, 0 for a character it can't draw, or -1 before the
 * framebuffer is mapped. */
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

/** Writes a string. Returns 0. */
static int earlycon_fb_puts(char *s)
{
    while (*s)
        earlycon_fb_putc(*s++);

    return 0;
}

/** Maps the framebuffer in the kernel's address space, and sizes the text
 * screen from its resolution. */
static void earlycon_fb_init()
{
    fb_vmr.paddr = fb_paddr;
    fb_vmr.size  = fb_size;
    /* Cached, as it has always been: uncached memory is slow to read,
     * and scrolling reads it; write-combining would need the PAT set
     * up. */
    fb_vmr.flags = VM_KRW;

    vm_map(&kvm_space, &fb_vmr);
    fb_init = 1;

    fb_cols = fb_xres / 8;
    fb_rows = fb_yres / 16;
}

struct earlycon earlycon_fb = {
    .init = earlycon_fb_init,
    .putc = earlycon_fb_putc,
    .puts = earlycon_fb_puts,
};
