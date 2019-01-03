/*
 *          IBM VGA Text-Mode Console
 *
 *
 *  This file is part of Aquila OS and is released under
 *  the terms of GNU GPLv3 - See LICENSE.
 *
 */


#include <core/system.h>
#include <core/module.h>
#include <core/string.h>

#include <cpu/io.h>

#include <mm/mm.h>

#include <dev/dev.h>
#include <fs/devfs.h>
#include <fs/posix.h>

#include <ds/queue.h>

#define VGA_START   (VMA((char*)0xB8000))   /* FIXME */

static char *vga = VGA_START;
static struct ioaddr __console_ioaddr = {
    .addr = 0x3D4,
    .type = IOADDR_PORT,
};

static void scroll(int n)
{
    memcpy(VGA_START, VGA_START + 160 * n, 160 * (25 - n));
    vga = VGA_START + 160 * (25 - n);

    for (int i = 0; i < 80 * n; ++i) {
        vga[2 * i + 0] = '\0';
        vga[2 * i + 1] = 0x07;
    }
}

static void set_cursor(unsigned pos)
{
    io_out8(&__console_ioaddr, 0x00, 0x0F);
    io_out8(&__console_ioaddr, 0x01, (uint8_t)(pos & 0xFF));

    io_out8(&__console_ioaddr, 0x00, 0x0E);
    io_out8(&__console_ioaddr, 0x01, (uint8_t)((pos >> 8) & 0xFF));
}

static ssize_t console_write(struct devid *dd __unused, off_t offset __unused, size_t size, void *buf)
{
    //printk("console_write(dd=%p, offset=%d, size=%d, buf=%p)\n", dd, offset, size, buf);
    for (size_t i = 0; i < size; ++i) {
        
        char c = ((char *) buf)[i];

        switch (c) {
            case '\0':  /* Null-character */
                break;
            case '\n':  /* Newline */
                vga = VGA_START + (vga - VGA_START) / 160 * 160 + 160;
                if (vga - VGA_START >= 160 * 25)
                    scroll(1);
                break;
            case '\b':  /* Backspace */
                if (vga - VGA_START >= 2) {
                    vga -= 2;
                    *vga = 0;
                }
                break;
            default:
                if(vga - VGA_START >= 160 * 25)
                    scroll(1);
                *vga = c;
                vga += 2;
        }
    }

    set_cursor((vga - VGA_START)/2);

    return size;
}

static int console_probe()
{
    return 0;
}

struct dev condev = {
    .name = "condev",
    .probe = console_probe,
    .write = console_write,

    .fops  = {
        .open  = posix_file_open,
        .write = posix_file_write,
    },
};

MODULE_INIT(condev, console_probe, NULL)
