/*
 * MIT License
 *
 * Copyright (c) 2016-2017 Patrick Rudolph <siro@das-labor.org>
 * Copyright (c) 2018 Mohamed Anwar
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Based on x11/main.c.
 *
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <sys/ioctl.h>


#include <nk/nuklear.h>
#include <nk/window.h>
#define RAWFB_RGBX_8888
#define NK_RAWFB_IMPLEMENTATION
#include <nk/rawfb.h>
#include <aquila/fb.h>
#include <aquila/input.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define WALLPAPER       1
#define WINDOW_WIDTH    1024
#define WINDOW_HEIGHT   768

#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\n", stderr);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    /* Aquila framebuffer */
    void *fb = NULL;
    int xres, yres, pitch, status;
    status = nk_aquilafb_init("/dev/fb0", &xres, &yres, &pitch, &fb);

    if (status || !fb) {
        die("Failed to initialize framebuffer");
    }

    /* Window: About */
    struct window window_about;
    extern void window_about_init(struct window *window);
    window_about_init(&window_about);

    /* Compositor */
    long started;
    int running = 1;

    struct rawfb_context *rawfb;
    unsigned char tex_scratch[512 * 512];

    rawfb = nk_rawfb_init(fb, tex_scratch, xres, yres, pitch);

    if (!rawfb) {
        die("Failed to initialize rawfb");
        running = 0;
    }

    /* Input */
    extern void cb_keyboard(void *, int, int);
    extern void cb_mouse(void *, int, int, int);
    struct aq_input input;
    aq_input_init(&input, cb_keyboard, cb_mouse, rawfb);


#if WALLPAPER
    int wpx, wpy, wpn;
    void *wp_data = stbi_load("/usr/share/wallpapers/wallpaper.jpg", &wpx, &wpy, &wpn, 4);
    struct rawfb_image wp_handle;

    wp_handle.pixels = wp_data;
    wp_handle.w = wpx;
    wp_handle.h = wpy;
    wp_handle.pitch = wpx*4;
    wp_handle.format = NK_FONT_ATLAS_RGBA32;
#endif
    
    //set_style(&rawfb->ctx, THEME_DARK);

    int about_window = 1, calc_window = 0, term_window = 0, editor_window = 0;

    while (running) {
        /* Input */
        nk_input_begin(&rawfb->ctx);

        aq_input_handler(&input);

        nk_input_end(&rawfb->ctx);

        /* GUI */

        int barheight = 0.05 * WINDOW_HEIGHT;
        int rowheight = 0.75 * barheight;
        if (nk_begin(&rawfb->ctx, "bar", nk_rect(0, WINDOW_HEIGHT-barheight, WINDOW_WIDTH, barheight + 20), NK_WINDOW_BACKGROUND)) {
            nk_layout_row_static(&rawfb->ctx, rowheight, 80, INT_MAX);

            if (nk_button_text(&rawfb->ctx, "Exit", 4))
                running = 0;

            if (nk_button_text(&rawfb->ctx, "About", 5)) {
                about_window = 1;
                struct nk_window *win = nk_window_find(&rawfb->ctx, "About");
                if (win && win->flags & NK_WINDOW_HIDDEN) {
                    win->flags &= ~NK_WINDOW_HIDDEN;
                }
            }

            if (nk_button_text(&rawfb->ctx, "Calc", 4)) {
                calc_window = 1;
                struct nk_window *win = nk_window_find(&rawfb->ctx, "Calculator");
                if (win && win->flags & NK_WINDOW_HIDDEN) {
                    win->flags &= ~NK_WINDOW_HIDDEN;
                }
            }

            if (nk_button_text(&rawfb->ctx, "Editor", 6)) {
                editor_window = 1;
                struct nk_window *win = nk_window_find(&rawfb->ctx, "Editor");
                if (win && win->flags & NK_WINDOW_HIDDEN) {
                    win->flags &= ~NK_WINDOW_HIDDEN;
                }
            }
        }
        nk_end(&rawfb->ctx);

        struct window *win = &window_about;

        if (about_window) {
            if (nk_begin(&rawfb->ctx, win->title, win->bounds,
                NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_NO_SCROLLBAR|
                NK_WINDOW_CLOSABLE|NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE)) {

                struct nk_window *nk_win = rawfb->ctx.current;
                struct nk_rect content = nk_win->layout->clip;

                /* Input stack */
                if (rawfb->ctx.end == nk_win) {
                    if (nk_input_is_mouse_hovering_rect(&rawfb->ctx.input, content)) {
                        nk_input_begin(&win->rawfb->ctx);
                        float rel_x, rel_y;
                        rel_x = rawfb->ctx.input.mouse.pos.x - content.x;
                        rel_y = rawfb->ctx.input.mouse.pos.y - content.y;

                        nk_input_motion(&win->rawfb->ctx, rel_x, rel_y);

                        nk_input_end(&win->rawfb->ctx);
                    } else {
                        nk_input_begin(&win->rawfb->ctx);
                        nk_input_motion(&win->rawfb->ctx, -1, -1);
                        nk_input_end(&win->rawfb->ctx);
                    }
                }

                win->redraw(win);

                nk_push_custom(&nk_win->buffer, content, NULL, nk_handle_ptr(&win->image));
            }

            nk_end(&rawfb->ctx);

            if (nk_window_is_closed(&rawfb->ctx, "About"))
                about_window = 0;
        }

#if 0
        if (calc_window) {
            calculator(&rawfb->ctx);

            if (nk_window_is_closed(&rawfb->ctx, "Calculator"))
                calc_window = 0;
        }
#endif

        if (editor_window) {
            if (nk_begin(&rawfb->ctx, "Editor", nk_rect(50, 50, 400, 400),
                NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
                NK_WINDOW_CLOSABLE|NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE)) {

                static char box_buffer[512];
                static int  box_len;

                nk_layout_row_dynamic(&rawfb->ctx, 500, 1);
                nk_edit_string(&rawfb->ctx, NK_EDIT_BOX, box_buffer, &box_len, 512, nk_filter_default);
            }
            nk_end(&rawfb->ctx);

            if (nk_window_is_closed(&rawfb->ctx, "Editor"))
                editor_window = 0;
        }


        /* Draw framebuffer */
#if WALLPAPER
        nk_aquilafb_wallpaper(fb, &wp_handle);
#endif
        nk_rawfb_render(rawfb, nk_rgb(0xec,0xf0,0xf1), 0);

        /* Emulate framebuffer */
        nk_aquilafb_render(fb);
    }

    nk_rawfb_shutdown(rawfb);
    aq_input_end(&input);
    return 0;
}

