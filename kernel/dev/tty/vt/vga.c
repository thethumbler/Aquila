/** \file
 * Virtual terminals in VGA text mode, without a framebuffer.
 */

#include <core/system.h>
#include <cpu/io.h>
#include <mm/mm.h>
#include <dev/vt.h>

#define VGA_ROWS    25
#define VGA_COLS    80

static uint16_t *vga = (uint16_t *) VMA(0xB8000);

static struct ioaddr crtc = {
    .addr = 0x3D4,
    .type = IOADDR_PORT,
};

static void vga_draw(int row, int col, struct vt_cell cell)
{
    vga[row * VGA_COLS + col] = vt_vga_attr(cell) << 8 | cell.ch;
}

static void vga_cursor(int row, int col, int visible)
{
    /* a hidden cursor is past the end of the screen */
    unsigned pos = visible ? row * VGA_COLS + col : VGA_ROWS * VGA_COLS;

    io_out8(&crtc, 0x00, 0x0F);
    io_out8(&crtc, 0x01, pos & 0xFF);
    io_out8(&crtc, 0x00, 0x0E);
    io_out8(&crtc, 0x01, (pos >> 8) & 0xFF);
}

int vt_vga_probe(void)
{
    return 0;
}

struct vt_display vt_vga_display = {
    .name   = "VGA text mode",
    .rows   = VGA_ROWS,
    .cols   = VGA_COLS,
    .draw   = vga_draw,
    .cursor = vga_cursor,
};
