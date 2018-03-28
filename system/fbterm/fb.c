#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fb.h>

#define _NJ_INCLUDE_HEADER_ONLY
#include "nanojpeg.c"

/* Framebuffer API */
static struct fb_fix_screeninfo fix_screeninfo;
static struct fb_var_screeninfo var_screeninfo;
static int fb = -1;
static unsigned xres = 0, yres = 0, line_length = 0, bpp = 0;

#define COLORMERGE(f, b, c)	((b) + (((f) - (b)) * (c) >> 8u))
#define _R(c)   (((c) >> 3*8) & 0xFF)
#define _G(c)   (((c) >> 2*8) & 0xFF)
#define _B(c)   (((c) >> 1*8) & 0xFF)
#define _A(c)   (((c) >> 0*8) & 0xFF)

void fb_put_pixel(struct fbterm_ctx *ctx, int x, int y, uint32_t fg, uint32_t bg)
{
    unsigned char a = _A(fg);
    unsigned char *p = &ctx->textbuf[y * line_length + bpp*x];

    p[0] = COLORMERGE(_R(fg), _R(bg), a);  /* Red */
    p[1] = COLORMERGE(_G(fg), _G(bg), a);  /* Green */
    p[2] = COLORMERGE(_B(fg), _B(bg), a);  /* Blue */

    unsigned char *q = &ctx->backbuf[y * line_length + bpp*x];
    q[0] = COLORMERGE(p[0], q[0], ctx->op);  /* Red */
    q[1] = COLORMERGE(p[1], q[1], ctx->op);  /* Green */
    q[2] = COLORMERGE(p[2], q[2], ctx->op);  /* Blue */
}

void fb_clear(struct fbterm_ctx *ctx)
{
    memset(ctx->textbuf, 0, yres*line_length);
    memset(ctx->backbuf, 0, yres*line_length);

    if (ctx->wallpaper)
        memcpy(ctx->backbuf, ctx->wallpaper, yres*line_length);
}

void fb_rect_clear(struct fbterm_ctx *ctx, size_t x0, size_t x1, size_t y0, size_t y1)
{
    for (size_t y = y0; y < y1; ++y) {
        memset(ctx->textbuf + (y * line_length) + x0 * bpp, 0, (x1 - x0) * bpp);
        memset(ctx->backbuf + (y * line_length) + x0 * bpp, 0, (x1 - x0) * bpp);
    }

    if (ctx->wallpaper) {
        for (size_t y = y0; y < y1; ++y) {
            size_t off = (y * line_length) + x0 * bpp;
            memcpy(ctx->backbuf + off, ctx->wallpaper + off, (x1 - x0) * bpp);
        }
    }
}

void fb_rect_move(struct fbterm_ctx *ctx, size_t dx0, size_t dx1, size_t dy0, size_t dy1,
        size_t sx0, size_t sx1, size_t sy0, size_t sy1)
{
    for (size_t y = dy0; y < dy1; ++y) {
        memset(ctx->backbuf + (y * line_length) + dx0 * bpp, 0, (dx1 - dx0) * bpp);
    }

    if (ctx->wallpaper) {
        for (size_t y = dy0; y < dy1; ++y) {
            size_t off = (y * line_length) + dx0 * bpp;
            memcpy(ctx->backbuf + off, ctx->wallpaper + off, (dx1 - dx0) * bpp);
        }
    }

    for (size_t y = 0; y < (dy1 - dy0); ++y) {
        memmove(ctx->textbuf + ((dy0 + y) * line_length) + dx0 * bpp,
                ctx->textbuf + ((sy0 + y) * line_length) + sx0 * bpp,
                (sx1 - sx0) * bpp);
    }

    for (size_t y = dy0; y < dy1; ++y) {
        for (size_t x = dx0; x < dx1; ++x) {
            unsigned char *p = &ctx->textbuf[y * line_length + bpp*x];
            unsigned char *q = &ctx->backbuf[y * line_length + bpp*x];
            q[0] = COLORMERGE(p[0], q[0], ctx->op);  /* Red */
            q[1] = COLORMERGE(p[1], q[1], ctx->op);  /* Green */
            q[2] = COLORMERGE(p[2], q[2], ctx->op);  /* Blue */
        }
    }
}

void fb_render(struct fbterm_ctx *ctx)
{
    lseek(fb, 0, SEEK_SET);
    write(fb, ctx->backbuf, line_length * yres);
}

void fb_term_init(struct fbterm_ctx *ctx)
{
    ctx->op   = 200;
    ctx->rows = yres/ctx->font->rows;
    ctx->cols = xres/ctx->font->cols;
    ctx->textbuf = calloc(1, yres * line_length);
    ctx->backbuf = calloc(1, yres * line_length);
}

int fb_cook_wallpaper(struct fbterm_ctx *ctx, char *path)
{
    int img = open(path, O_RDONLY);
    size_t size = lseek(img, 0, SEEK_END);
    lseek(img, 0, SEEK_SET);

    char *buf = calloc(1, size);
    read(img, buf, size);
    close(img);

    njInit();
    int err = 0;

    if (err = njDecode(buf, size)) {
        free(buf);
        fprintf(stderr, "Error decoding input file: %d\n", err);
        return -1;
    }

    free(buf);
    size_t height = njGetHeight();
    size_t width  = njGetWidth();
    size_t cook_height, cook_width;
    char *img_buf = njGetImage();
    size_t ncomp = njGetImageSize() / (height * width);
    size_t img_line_length = width * ncomp;
    size_t xpan = 0, ypan = 0, xoffset = 0, yoffset = 0;

    /* Pan or crop image to match screen size */
    if (width < xres) {
        xpan = (xres-width)/2;
        cook_width = width;
    } else {
        xoffset = (width-xres)/2;
        cook_width = xres;
    }

    if (height < yres) {
        ypan = (yres-height)/2;
        cook_height = height;
    } else {
        yoffset = (height-yres)/2;
        cook_height = yres;
    }

    ctx->wallpaper = calloc(1, yres*line_length);

#define WP_POS(i, j) (((i) + ypan) * line_length + ((j) + xpan) * bpp)
#define IMG_POS(i, j) (((i) + yoffset) * img_line_length + ((j) + xoffset) * ncomp)

    for (size_t i = 0; i < cook_height; ++i) {
        for (size_t j = 0; j < cook_width; ++j) {
            ctx->wallpaper[WP_POS(i, j) + 2] = img_buf[IMG_POS(i, j) + 0];
            ctx->wallpaper[WP_POS(i, j) + 1] = img_buf[IMG_POS(i, j) + 1];
            ctx->wallpaper[WP_POS(i, j) + 0] = img_buf[IMG_POS(i, j) + 2];
        }
    }

    njDone();

    memcpy(ctx->backbuf, ctx->wallpaper, yres*line_length);
    return 0;
}

char *dbuf = NULL;
void fb_debug(char r, char g, char b)
{
    if (!dbuf)
        dbuf = calloc(1, line_length * yres);

    for (size_t y = 0; y < yres; ++y) {
        for (size_t x = 0; x < xres; ++x) {
            dbuf[y * line_length + bpp * x + 0] = r;
            dbuf[y * line_length + bpp * x + 1] = g;
            dbuf[y * line_length + bpp * x + 2] = b;
        }
    }

    lseek(fb, 0, SEEK_SET);
    write(fb, dbuf, line_length * yres);
}

int fb_init(char *path)
{
    if ((fb = open(path, O_RDWR)) < 0) {
        //fprintf(stderr, "Error opening framebuffer %s", path);
        //perror("");
        return errno;
    }

    if (ioctl(fb, FBIOGET_FSCREENINFO, &fix_screeninfo) < 0) {
        //perror("ioctl error");
        return errno;
    }

    line_length = fix_screeninfo.line_length;

    if (ioctl(fb, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
        //perror("ioctl error");
        return errno;
    }

    xres = var_screeninfo.xres;
    yres = var_screeninfo.yres;

    bpp  = var_screeninfo.bits_per_pixel/8;

    return 0;
}
