#include <stdlib.h>

#include <aquila/fb.h>

#define NCOMP   4

struct aquilafb_image *wallpaper_set(const char *path)
{
    static void *buf = NULL;
    static struct aquilafb_image handle;

    int width, height, ncomp;

    if (buf) {
        free(buf);
        buf = NULL;
    }

    buf = stbi_load(path, &width, &height, &ncomp, NCOMP);

    handle.buf   = buf;
    handle.xres  = width;
    handle.yres  = height;
    handle.pitch = width * NCOMP;
    handle.bpp   = NCOMP;

    return &handle;
}
