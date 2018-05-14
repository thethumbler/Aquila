#ifndef _AQUILA_FB_H
#define _AQUILA_FB_H

#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

static struct fb_fix_screeninfo fix_screeninfo;
static struct fb_var_screeninfo var_screeninfo;
static int fb_fd = -1;
static unsigned xres = 0, yres = 0, line_length = 0, bpp = 0;

/* Framebuffer API */

#define FBIOGET_VSCREENINFO	0x4600
#define FBIOPUT_VSCREENINFO	0x4601
#define FBIOGET_FSCREENINFO	0x4602

struct fb_fix_screeninfo {
	char id[16];			/* identification string eg "TT Builtin" */
	unsigned long smem_start;	/* Start of frame buffer mem */
					/* (physical address) */
	uint32_t smem_len;			/* Length of frame buffer mem */
	uint32_t type;			/* see FB_TYPE_*		*/
	uint32_t type_aux;			/* Interleave for interleaved Planes */
	uint32_t visual;			/* see FB_VISUAL_*		*/
	uint16_t xpanstep;			/* zero if no hardware panning  */
	uint16_t ypanstep;			/* zero if no hardware panning  */
	uint16_t ywrapstep;		/* zero if no hardware ywrap    */
	uint32_t line_length;		/* length of a line in bytes    */
	unsigned long mmio_start;	/* Start of Memory Mapped I/O   */
					/* (physical address) */
	uint32_t mmio_len;			/* Length of Memory Mapped I/O  */
	uint32_t accel;			/* Indicate to driver which	*/
					/*  specific chip/card we have	*/
	uint16_t capabilities;		/* see FB_CAP_*			*/
	uint16_t reserved[2];		/* Reserved for future compatibility */
};

struct fb_bitfield {
    uint32_t offset;    /* beginning of bitfield    */
    uint32_t length;    /* length of bitfield       */
    uint32_t msb_right; /* != 0 : Most significant bit is right */
};

struct fb_var_screeninfo {
    uint32_t xres;          /* visible resolution       */
    uint32_t yres;
    uint32_t xres_virtual;  /* virtual resolution       */
    uint32_t yres_virtual;
    uint32_t xoffset;       /* offset from virtual to visible */
    uint32_t yoffset;       /* resolution           */

    uint32_t bits_per_pixel;    /* guess what           */
    uint32_t grayscale;     /* 0 = color, 1 = grayscale, >1 = FOURCC          */
    struct fb_bitfield red;     /* bitfield in fb mem if true color, */
    struct fb_bitfield green;   /* else only length is significant */
    struct fb_bitfield blue;
    struct fb_bitfield transp;  /* transparency         */

    uint32_t nonstd;            /* != 0 Non standard pixel format */

    uint32_t activate;          /* see FB_ACTIVATE_*        */

    uint32_t height;            /* height of picture in mm    */
    uint32_t width;         /* width of picture in mm     */

    uint32_t accel_flags;       /* (OBSOLETE) see fb_info.flags */

    /* Timing: All values in pixclocks, except pixclock (of course) */
    uint32_t pixclock;          /* pixel clock in ps (pico seconds) */
    uint32_t left_margin;       /* time from sync to picture    */
    uint32_t right_margin;      /* time from picture to sync    */
    uint32_t upper_margin;      /* time from sync to picture    */
    uint32_t lower_margin;
    uint32_t hsync_len;     /* length of horizontal sync    */
    uint32_t vsync_len;     /* length of vertical sync  */
    uint32_t sync;          /* see FB_SYNC_*        */
    uint32_t vmode;         /* see FB_VMODE_*       */
    uint32_t rotate;            /* angle we rotate counter clockwise */
    uint32_t colorspace;        /* colorspace for FOURCC-based modes */
    uint32_t reserved[4];       /* Reserved for future compatibility */
};

NK_API int nk_aquilafb_init(char *path, int *aqfb_xres, int *aqfb_yres, int *aqfb_pitch, void **fb)
{
    if ((fb_fd = open(path, O_RDWR)) < 0) {
        fprintf(stderr, "Error opening framebuffer %s", path);
        perror("");
        return errno;
    }

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix_screeninfo) < 0) {
        //perror("ioctl error");
        return errno;
    }

    line_length = fix_screeninfo.line_length;

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
        perror("ioctl error");
        return errno;
    }

    xres = var_screeninfo.xres;
    yres = var_screeninfo.yres;
    bpp  = var_screeninfo.bits_per_pixel/8;

    *aqfb_xres = xres;
    *aqfb_yres = yres;
    *aqfb_pitch = xres * bpp;

    *fb = calloc(1, yres * line_length);

    return 0;
}

NK_API void nk_aquilafb_wallpaper(char *fb, struct rawfb_image *wp)
{
    char *p = wp->pixels;

    for (int i = 0; i < yres; ++i) {
        //memcpy(fb + i * line_length, p + i * wp->pitch, line_length);
        
        for (int j = 0; j < xres; ++j) {
            fb[i * line_length + bpp * j + 0] = p[i * wp->pitch + 4 * j + 2];
            fb[i * line_length + bpp * j + 1] = p[i * wp->pitch + 4 * j + 1];
            fb[i * line_length + bpp * j + 2] = p[i * wp->pitch + 4 * j + 0];
        }
    }
}

NK_API void nk_aquilafb_clear(char *fb, struct nk_color color)
{
    char pixel[bpp];
    pixel[0] = color.b;
    pixel[1] = color.g;
    pixel[2] = color.r;

    for (int i = 0; i < yres; ++i) {
        for (int j = 0; j < xres; ++j) {
            fb[i * line_length + bpp * j + 0] = pixel[0];
            fb[i * line_length + bpp * j + 1] = pixel[1];
            fb[i * line_length + bpp * j + 2] = pixel[2];
        }
    }

#if 0
    /* Create line */
    char line[line_length];
    for (int i = 0; i < xres; ++i)
        memcpy(line[i * bpp], pixel, 3);

    /* Fill */
    for (int i = 0; i < yres; ++i)
        memcpy(fb + i * line_length, line, line_length);
#endif
}

NK_API void nk_aquilafb_render(void *fb)
{
    lseek(fb_fd, 0, SEEK_SET);
    write(fb_fd, fb, line_length * yres);
}

#define _RGBA(r, g, b, a) (((r) << 3*8) | ((g) << 2*8) | ((b) << 1*8) | ((a) << 0*8))
#define _RGB(r, g, b) (((r) << 3*8) | ((g) << 2*8) | ((b) << 1*8))
#define _ALPHA(c, a) (((c) & (~0xFF)) | ((a) & 0xFF))

#endif
