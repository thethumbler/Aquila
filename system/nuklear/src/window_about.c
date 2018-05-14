#include <limits.h>
#include <nk/nuklear.h>
#include <nk/window.h>
#include <nk/rawfb.h>

//#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define LOGO_PATH "/usr/share/nuklear/logo.png"
static struct nk_image aquila_logo;

void window_about_init(struct window *window);
void window_about_redraw(struct window *window);

void window_about_init(struct window *window)
{
    int w = 400, h = 400;

    window->title  = "About";
    window->bounds = nk_rect(50, 50, w, h);

    void *fb = malloc(h * w * 4);
    static char tex[512 * 512];

    window->rawfb  = nk_rawfb_init(fb, tex, w, h, w * 4);

    window->redraw = window_about_redraw;

    window->image  = nk_image_ptr(&window->rawfb->fb);
    window_image_init(window, &window->image);

    nk_style_hide_cursor(&window->rawfb->ctx);

    memset(fb, 0, w * h * 4);

    int logo_w, logo_h, logo_n;
    void *logo_data = stbi_load(LOGO_PATH, &logo_w, &logo_h, &logo_n, 4);

    static struct rawfb_image handle;

    handle.pixels = logo_data;
    handle.w = logo_w;
    handle.h = logo_h;
    handle.pitch = logo_w * 4;
    handle.format = NK_FONT_ATLAS_RGBA32;

    aquila_logo = nk_image_ptr(&handle);
    aquila_logo.w = logo_w;
    aquila_logo.h = logo_h;

    aquila_logo.region[0] = 0;
    aquila_logo.region[1] = 0;
    aquila_logo.region[2] = 80;
    aquila_logo.region[3] = 80;
}

void window_about_redraw(struct window *window)
{
    struct rawfb_context *rawfb = window->rawfb;
    if (nk_begin(&rawfb->ctx, "About", nk_rect(0, 0, window->bounds.w, window->bounds.h), NK_WINDOW_BACKGROUND)) {
#if 1
        /* Draw the logo */
        nk_layout_space_begin(&rawfb->ctx, NK_STATIC, 80, INT_MAX);
        nk_layout_space_push(&rawfb->ctx, nk_rect(150,0,80,80));
        nk_image(&rawfb->ctx, aquila_logo);
        nk_layout_space_end(&rawfb->ctx);

        nk_layout_row_dynamic(&rawfb->ctx, 30, 1);
        nk_label(&rawfb->ctx, "NuklearUI", NK_TEXT_CENTERED);
        nk_label(&rawfb->ctx, "Based on Nuklear GUI Library", NK_TEXT_CENTERED);
        nk_label(&rawfb->ctx, "By Micha Mettke", NK_TEXT_CENTERED);

        nk_label(&rawfb->ctx, "", NK_TEXT_CENTERED);
        nk_label(&rawfb->ctx, "AquilaOS v0.0.1a", NK_TEXT_CENTERED);
        nk_label(&rawfb->ctx, "By Mohamed Anwar", NK_TEXT_CENTERED);
#endif
    }

    nk_end(&rawfb->ctx);
    nk_rawfb_render(rawfb, nk_rgb(0xec,0xf0,0xf1), 1);
}
