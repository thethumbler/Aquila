#ifndef _NK_WINDOWN_H
#define _NK_WINDOWN_H

#include <nk/nuklear.h>
#include <nk/rawfb.h>

struct window {
    char *title;
    struct rawfb_context *rawfb;
    struct nk_rect bounds;
    struct nk_image image;

    void (*redraw)(struct window *);
};

static void window_image_init(struct window *window, struct nk_image *image)
{
    image->w = window->bounds.w;
    image->h = window->bounds.h;
    image->region[0] = 0;
    image->region[1] = 0;
    image->region[2] = window->bounds.w;
    image->region[3] = window->bounds.h;
}

#endif
