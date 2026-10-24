/** \file
 * x86: the early console in VGA text mode, written straight into the
 * text buffer at 0xB8000 (through its i386 kernel address).
 */

#include <core/system.h>
#include <core/string.h>
#include <console/earlycon.h>
#include <cpu/io.h>

static char *vga_start = (char*) 0xC00B8000;
static char *vga = (char*) 0xC00B8000;

struct ioaddr vga_ioaddr = {
    .addr = 0x3D4,
    .type = IOADDR_PORT,
};

#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_SIZE    (2 * VGA_COLS * VGA_ROWS)

/** Scrolls the screen up by `n` lines, clearing the lines at the
 * bottom. */
static void earlycon_vga_scroll(int n)
{
    memmove(vga_start, vga_start + 160 * n, 160 * (VGA_ROWS - n));
    vga = vga_start + 160 * (VGA_ROWS - n);

    int i;
    for (i = 0; i < VGA_COLS * n; ++i) {
        vga[2 * i + 0] = '\0';
        vga[2 * i + 1] = 0x07;
    }
}

/** Writes a character at the cursor, scrolling when it goes past the last
 * line. Returns 1, or 0 for a NUL. */
static int earlycon_vga_putc(char c)
{
    if (c) {
        if (c == '\n')
            vga = vga_start + (vga - vga_start) / 160 * 160 + 160;
        else {
            *vga = c;
            vga += 2;
        }

        /* wrapped past the last line */
        if (vga - vga_start >= VGA_SIZE)
            earlycon_vga_scroll(1);

        return 1;
    }

    return 0;
}

/** Writes a string, and returns how many characters it wrote. */
static int earlycon_vga_puts(char *s)
{
    char *_s = s;

    while (*s)
        earlycon_vga_putc(*s++);

    return (int)(s - _s);
}

/** Hides the hardware cursor, and clears the screen. */
static void earlycon_vga_init()
{
    io_out8(&vga_ioaddr, 0x00, 0x0F);
    io_out8(&vga_ioaddr, 0x01, 0xFF);
    io_out8(&vga_ioaddr, 0x00, 0x0E);
    io_out8(&vga_ioaddr, 0x01, 0xFF);

    unsigned i;
    for (i = 0; i < 80*25; ++i)
        vga_start[2*i] = 0;
}

struct earlycon earlycon_vga = {
    .init = earlycon_vga_init,
    .putc = earlycon_vga_putc,
    .puts = earlycon_vga_puts,
};
