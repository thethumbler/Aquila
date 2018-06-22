#include <aquila/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#include <nk/rawfb.h> /* FIXME */

#if AQUILAFB_DEBUG
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...)
#endif

int aquilafb_init(struct aquilafb *fb, char *path)
{
    if (!fb || !path) {
        errno = EINVAL;
        DEBUG("Error opening framebuffer %s: %s", path, strerror(errno));
        return -1;
    }

    if ((fb->fd = open(path, O_RDWR)) < 0) {
        DEBUG(stderr, "Error opening framebuffer %s: %s", path, strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix_screeninfo) < 0) {
        DEBUG(stderr, "Framebuffer ioctl error: %s", strerror(errno));
        return -1;
    }

    fb->pitch = fb->fix_screeninfo.line_length;

    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var_screeninfo) < 0) {
        DEBUG(stderr, "Framebuffer ioctl error: %s", strerror(errno));
        return -1;
    }

    fb->xres = fb->var_screeninfo.xres;
    fb->yres = fb->var_screeninfo.yres;
    fb->bpp  = fb->var_screeninfo.bits_per_pixel/8;

    //fb->backbuf = calloc(1, fb->yres * fb->pitch);

#if 0
    if (mmap(mmap_fb, line_length * yres, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fb_fd, 0) == MAP_FAILED) {
        perror("mmap");
        return errno;
    }
#endif

    return 0;
}

void aquilafb_wallpaper(struct aquilafb *fb, char *backbuf, struct aquilafb_image *img)
{
    char *p = img->buf;

    for (int i = 0; i < fb->yres; ++i) {
        //memcpy(fb + i * line_length, p + i * wp->pitch, line_length);
        
        for (int j = 0; j < fb->xres; ++j) {
            backbuf[i * fb->pitch + fb->bpp * j + 0] = p[i * img->pitch + img->bpp * j + 2];
            backbuf[i * fb->pitch + fb->bpp * j + 1] = p[i * img->pitch + img->bpp * j + 1];
            backbuf[i * fb->pitch + fb->bpp * j + 2] = p[i * img->pitch + img->bpp * j + 0];
        }
    }
}

#if 0
void aquilafb_clear(struct aquilafb *fb, struct nk_color color)
{
    char pixel[fb->bpp];
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
#endif

void aquilafb_render(struct aquilafb *fb, void *backbuf)
{
#if 0
    memcpy(mmap_fb, fb, line_length * yres);
#else
    lseek(fb->fd, 0, SEEK_SET);
    write(fb->fd, backbuf, fb->pitch * fb->yres);
#endif
}
