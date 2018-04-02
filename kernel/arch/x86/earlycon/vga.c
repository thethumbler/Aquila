#include <stdio.h>

char *vga_start = (char*) 0xC00B8000;
char *vga = (char*) 0xC00B8000;

struct __ioaddr __vga_ioaddr = {
    .addr = 0x3D4,
    .type = __IOADDR_PORT,
}

static void early_console_scroll(int n)
{
    memcpy(vga_start, vga_start + 160 * n, 160 * (25 - n));
    vga = vga_start + 160 * (25 - n);

    int i;
    for (i = 0; i < 80 * n; ++i) {
        vga[2 * i + 0] = '\0';
        vga[2 * i + 1] = 0x07;
    }
}

int early_console_putc(char c)
{
    if (c) {
        if (c == '\n') {
            vga = vga_start + (vga - vga_start) / 160 * 160 + 160;
            if(vga - vga_start >= 160 * 25)
                early_console_scroll(1);
            return 1;
        }

        *vga = c;
        vga += 2;
        return 1;
    }
    return 0;
}

int earlycon_vga_puts(char *s)
{
    char *_s = s;
    while (*s) {
        *vga = *s++;
        vga += 2;
    }

    return (int)(s - _s);
}

void earlycon_vga_init()
{
    outb(0x3D4, 0x0F);
    outb(0x3D5, 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, 0xFF);

    unsigned i;
    for(i = 0; i < 80*25; ++i)
        vga_start[2*i] = 0;
}
