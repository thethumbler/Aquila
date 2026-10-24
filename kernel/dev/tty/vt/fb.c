/** \file
 * Virtual terminals on the framebuffer.
 *
 * Each cell of the terminal is drawn as a character of the early
 * console's 8x16 font, in the cell's colours, pixel by pixel. Framebuffers
 * of 16, 24 and 32 bits per pixel are supported. The virtual terminals
 * (`vt.c`) draw through `vt_fb_display`.
 */

#include <core/system.h>
#include <core/string.h>
#include <dev/fbdev.h>
#include <dev/vt.h>

#define FONT_W  8
#define FONT_H  16

/* the font: code page 437, from the early console */
extern const unsigned char font_8x16[];

/* the framebuffer device, if the kernel has a framebuffer driver */
extern struct fbdev *fbdev_primary(void) __attribute__((weak));

static uint8_t *fb;
static uint32_t pitch, bytes_pp;

/** Writes one pixel of colour `rgb` (0xRRGGBB) at `p`, in the
 * framebuffer's pixel format. */
static inline void put_pixel(uint8_t *p, uint32_t rgb)
{
    switch (bytes_pp) {
        case 4:
            *(uint32_t *) p = rgb;
            break;
        case 3:
            p[0] = rgb;
            p[1] = rgb >> 8;
            p[2] = rgb >> 16;
            break;
        case 2:
            *(uint16_t *) p = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
            break;
    }
}

/** Draws one cell of the terminal: its character's glyph, in its colours
 * and attributes (bold, reverse, dim, invisible, underline, cursor). */
static void fb_draw(int row, int col, struct vt_cell cell)
{
    uint8_t fg = cell.fg, bg = cell.bg;

    if ((cell.attr & VT_BOLD) && fg < 8)
        fg += 8;

    if (cell.attr & VT_REVERSE) {
        uint8_t t = fg;
        fg = bg;
        bg = t;
    }

    uint32_t fg_rgb = vt_palette(fg), bg_rgb = vt_palette(bg);

    if (cell.attr & VT_DIM)
        fg_rgb = (fg_rgb >> 1) & 0x7f7f7f;

    if (cell.attr & VT_INVISIBLE)
        fg_rgb = bg_rgb;

    const uint8_t *glyph = &font_8x16[cell.ch * FONT_H];
    uint8_t *line = fb + row * FONT_H * pitch + col * FONT_W * bytes_pp;

    for (int y = 0; y < FONT_H; ++y, line += pitch) {
        uint8_t bits = glyph[y];

        if ((cell.attr & VT_UNDERLINE) && y == FONT_H - 2)
            bits = 0xff;

        /* an underline cursor, like the Linux console's */
        if ((cell.attr & VT_CURSOR) && y >= FONT_H - 2)
            bits = 0xff;

        if (bytes_pp == 4) {
            uint32_t *p = (uint32_t *) line;
            for (int x = 0; x < FONT_W; ++x)
                p[x] = bits & (0x80 >> x) ? fg_rgb : bg_rgb;
        } else {
            for (int x = 0; x < FONT_W; ++x)
                put_pixel(line + x * bytes_pp, bits & (0x80 >> x) ? fg_rgb : bg_rgb);
        }
    }
}

/**
 * Sets up the virtual terminals on the framebuffer, if there is one the
 * terminals can draw on, and clears the screen.
 *
 * @return 0, or `-ENODEV` if there is no framebuffer, or not one of 16,
 *         24 or 32 bits per pixel
 */
int vt_fb_probe(void)
{
    struct fbdev *dev = fbdev_primary ? fbdev_primary() : NULL;

    if (!dev || !dev->fix_screeninfo || !dev->var_screeninfo)
        return -ENODEV;

    struct fb_fix_screeninfo *fix = dev->fix_screeninfo;
    struct fb_var_screeninfo *var = dev->var_screeninfo;

    if (var->bits_per_pixel != 32 && var->bits_per_pixel != 24 && var->bits_per_pixel != 16)
        return -ENODEV;

    fb = (uint8_t *) fix->smem_start;
    pitch = fix->line_length;
    bytes_pp = var->bits_per_pixel / 8;

    vt_fb_display.rows = var->yres / FONT_H;
    vt_fb_display.cols = var->xres / FONT_W;

    if (!vt_fb_display.rows || !vt_fb_display.cols)
        return -ENODEV;

    /* clear the whole screen: the terminals don't draw on the margins,
     * and the early console may have */
    memset(fb, 0, var->yres * pitch);

    return 0;
}

struct vt_display vt_fb_display = {
    .name = "framebuffer",
    .draw = fb_draw,
};
