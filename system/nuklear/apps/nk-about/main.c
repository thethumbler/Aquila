#include <libnk/libnk.h>
#include <limits.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define LOGO_PATH "/usr/share/nuklear/logo.png"

int main()
{
    struct libnk_context libnk_ctx;
    libnk_connect(&libnk_ctx, NKUI_DEFAULT_PATH);

    struct libnk_window window;
    libnk_window_init(&libnk_ctx, &window, "About", nk_rect(40, 40, 400, 400), 
        NK_WINDOW_TITLE|NK_WINDOW_BORDER|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_MOVABLE|
            NK_WINDOW_MINIMIZABLE|NK_WINDOW_CLOSABLE);

    libnk_window_create(&libnk_ctx, &window);

    int logo_w, logo_h, logo_n;
    void *logo_data = stbi_load(LOGO_PATH, &logo_w, &logo_h, &logo_n, 4);

    struct libnk_pixbuf pixbuf;
    libnk_pixbuf_create(&libnk_ctx, &pixbuf, logo_w, logo_h);
    libnk_pixbuf_push(&libnk_ctx, &pixbuf, nk_rect(0, 0, logo_w, logo_h), logo_data);

    struct nk_image aquila_logo;
    aquila_logo   = nk_image_ptr(pixbuf.id);
    aquila_logo.w = logo_w;
    aquila_logo.h = logo_h;

    aquila_logo.region[0] = 0;
    aquila_logo.region[1] = 0;
    aquila_logo.region[2] = 80;
    aquila_logo.region[3] = 80;

    struct nk_context *ctx;
    if ((ctx = libnk_begin(&libnk_ctx, &window))) {
        /* Draw the logo */
        nk_layout_space_begin(ctx, NK_STATIC, 80, INT_MAX);
        nk_layout_space_push(ctx, nk_rect(150,0,80,80));
        nk_image(ctx, aquila_logo);
        nk_layout_space_end(ctx);

        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "NuklearUI", NK_TEXT_CENTERED);
        nk_label(ctx, "Based on Nuklear GUI Library", NK_TEXT_CENTERED);
        nk_label(ctx, "By Micha Mettke", NK_TEXT_CENTERED);

        nk_label(ctx, "", NK_TEXT_CENTERED);
        nk_label(ctx, "AquilaOS v0.0.1a", NK_TEXT_CENTERED);
        nk_label(ctx, "By Mohamed Anwar", NK_TEXT_CENTERED);
    }

    libnk_end(&libnk_ctx, &window);

    libnk_render(&libnk_ctx, &window);

    /* TODO events */

    for (;;);
}
