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

static void earlycon_vga_scroll(int n)
{
    memcpy(vga_start, vga_start + 160 * n, 160 * (25 - n));
    vga = vga_start + 160 * (25 - n);

    int i;
    for (i = 0; i < 80 * n; ++i) {
        vga[2 * i + 0] = '\0';
        vga[2 * i + 1] = 0x07;
    }
}

static int earlycon_vga_putc(char c)
{
    if (c) {
        if (c == '\n') {
            vga = vga_start + (vga - vga_start) / 160 * 160 + 160;
            if (vga - vga_start >= 160 * 25)
                earlycon_vga_scroll(1);
            return 1;
        }

        *vga = c;
        vga += 2;
        return 1;
    }

    return 0;
}

static int earlycon_vga_puts(char *s)
{
    char *_s = s;
    while (*s) {
        *vga = *s++;
        vga += 2;
    }

    return (int)(s - _s);
}

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
